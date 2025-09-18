#include "Flatten.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <utility>
#include <vector>
#include <vendor/Eigen/Dense>

// This is an emulation, in spirit at least, of the effect of of the
// Fortran flat4() subroutine. While our implementation differs from
// that of the original, the results should be as good or better.
//
// One key difference other than what's obvious below is that this
// isn't responsible for converting a power-scaled spectrum to dB.
// If you need to do that before sending data here, that's on you;
// std::transform and std::log10 are going to be your friends. We
// do flattening, and only flattening.
//
// Note that this is a functor; it's serially reusable, but it's not
// reentrant. Call it from one thread only. In practical use, that's
// not expected to be a problem, and it allows us to reuse allocated
// memory in a serial manner, rather than requesting it and freeing
// it constantly.

/******************************************************************************/
// Flatten Constants
/******************************************************************************/

namespace
{
  // Tunable settings; degree of the polynomial used for the baseline
  // curve fit, and the percentile of the span at which to sample. In
  // general, a 5th degree polynomial and the 10th percentile should
  // be optimal.

  constexpr auto FLATTEN_DEGREE =  5;
  constexpr auto FLATTEN_SAMPLE = 10;

  // We're going to do a pairwise Estrin's evaluation of the polynomial
  // coefficients, so it's critical that the degree of the polynomial is
  // odd, resulting in an even number of coefficients.

  static_assert(FLATTEN_DEGREE &  1,   "Degree must be odd");
  static_assert(FLATTEN_SAMPLE >= 0 &&
                FLATTEN_SAMPLE <= 100, "Sample must be a percentage");

  // Since we know the degree of the polynomial, and thus the number of
  // nodes that we're going to use, we can do all the trigonometry work
  // required to calculate the Chebyshev nodes in advance, by computing
  // them over the range [0, 1]; we can then scale these at runtime to
  // a span of any size by simple multiplication.
  //
  // Downside to this with C++17 is that std::cos() is not yet constexpr,
  // as it is in C++23, so we must provide our own implementation until
  // then.

  constexpr auto FLATTEN_NODES = []()
  {
    // Full-range cosine function using symmetries of cos(x).

    constexpr auto cos = [](double x)
    {
      constexpr auto RAD_360 = M_PI * 2;
      constexpr auto RAD_180 = M_PI;
      constexpr auto RAD_90  = M_PI_2;

      // Polynomial approximation of cos(x) for x in [0, RAD_90],
      // Accuracy here in theory is 1e-18, but double precision
      // itself is only 1-e16, so within the domain of doubles,
      // this should be extremely accurate.

      constexpr auto cos = [](double x)
      {
        constexpr std::array coefficients =
        {
           1.0,                             // Coefficient for x^0
          -0.49999999999999994,             // Coefficient for x^2
           0.041666666666666664,            // Coefficient for x^4
          -0.001388888888888889,            // Coefficient for x^6
           0.000024801587301587,            // Coefficient for x^8
          -0.00000027557319223986,          // Coefficient for x^10
           0.00000000208767569878681,       // Coefficient for x^12
          -0.00000000001147074513875176,    // Coefficient for x^14
           0.0000000000000477947733238733   // Coefficient for x^16
        };

        auto const x2  = x   * x;
        auto const x4  = x2  * x2;
        auto const x6  = x4  * x2;
        auto const x8  = x4  * x4;
        auto const x10 = x8  * x2;
        auto const x12 = x8  * x4;
        auto const x14 = x12 * x2;
        auto const x16 = x8  * x8;

        return coefficients[0]
             + coefficients[1] * x2
             + coefficients[2] * x4
             + coefficients[3] * x6
             + coefficients[4] * x8
             + coefficients[5] * x10
             + coefficients[6] * x12
             + coefficients[7] * x14
             + coefficients[8] * x16;
      };

      // Reduce x to [0, RAD_360)

      x -= static_cast<long long>(x / RAD_360) * RAD_360;

      // Map x to [0, RAD_180]

      if (x > RAD_180) x = RAD_360 - x;

      // Map x to [0, RAD_90] and evaluate the polynomial;
      // flip the sign for angles in the second quadrant.

      return x > RAD_90 ? -cos(RAD_180 - x) : cos(x);
    };

    // Down to the actual business of generating Chebyshev nodes
    // suitable for scaling; once we move to C++20 as the minimum
    // compiler, we can remove the cos() function above and instead
    // call std::cos() here, as it's required to be constexpr in
    // C++20 and above, and presumably it'll be of high quality.

    auto           nodes = std::array<double, FLATTEN_DEGREE + 1>{};
    constexpr auto slice = M_PI / (2.0 * nodes.size());

    for (std::size_t i = 0; i < nodes.size(); ++i)
    {
      nodes[i] = 0.5 * (1.0 - cos(slice * (2.0 * i + 1)));
    }

    return nodes;
  }();
}

/******************************************************************************/
// Private Implementation
/******************************************************************************/

class Flatten::Impl
{
  using Points       = Eigen::Matrix<double, FLATTEN_NODES.size(), 2>;
  using Vandermonde  = Eigen::Matrix<double, FLATTEN_NODES.size(),
                                             FLATTEN_NODES.size()>;
  using Coefficients = Eigen::Vector<double, FLATTEN_NODES.size()>;

  Points       p;
  Vandermonde  V;
  Coefficients c;

  // Polynomial evaluation using Estrin's method, loop is unrolled at
  // compile time. A compiler should emit SIMD instructions from what
  // it sees here when the optimizer is involved, but even without it,
  // we'll likely see fused multiply-add instructions.

  inline auto
  evaluate(float const x) const
  {
    return [this]<Eigen::Index... I>(std::size_t const i,
                                     std::integer_sequence<Eigen::Index, I...>)
    {
      auto baseline = 0.0;
      auto exponent = 1.0;

      ((baseline += (c[I * 2] + c[I * 2 + 1] * i) * exponent, exponent *= i * i), ...);

      return static_cast<float>(baseline);
    }(x, std::make_integer_sequence<Eigen::Index,
                                    Coefficients::SizeAtCompileTime / 2>{});
  }

public:

  void
  operator()(float     * const data,
             std::size_t const size)
  {
    // Loop invariants; sentinel one past the end of the range, and
    // the number of points in each of the arms on either side of a
    // node.

    auto const end = data + size;
    auto const arm = size / (2 * FLATTEN_NODES.size());

    // Collect lower envelope points; use Chebyshev node interpolants
    // to reduce Runge's phenomenon oscillations.

    for (std::size_t i = 0; i < FLATTEN_NODES.size(); ++i)
    {
      auto const node = size * FLATTEN_NODES[i];
      auto const base = data + static_cast<int>(std::round(node));
      auto       span = std::vector<float>(std::clamp(base - arm, data, end),
                                           std::clamp(base + arm, data, end));

      auto const n = span.size() * FLATTEN_SAMPLE / 100;

      std::nth_element(span.begin(), span.begin() + n, span.end());

      p.row(i) << node, span[n];
    }

    // Extract x and y values from points and prepare the Vandermonde
    // matrix, initializing the first column with 1 (x^0); remaining
    // columns are filled with the Schur product.

    Eigen::VectorXd x = p.col(0);
    Eigen::VectorXd y = p.col(1);

    V.col(0).setOnes();
    for (Eigen::Index i = 1; i < V.cols(); ++i)
    {
      V.col(i) = V.col(i - 1).cwiseProduct(x);
    }

    // Solve the least squares problem for polynomial coefficients;
    // evaluate the polynomial and subtract the baseline.

    c = V.colPivHouseholderQr().solve(y);

    for (std::size_t i = 0; i < size; ++i) data[i] -= evaluate(i);
  }
};

/******************************************************************************/
// Public Implementation
/******************************************************************************/

Flatten::Flatten(bool const flatten)
: m_impl(flatten ? std::make_unique<Impl>() : nullptr)
{}

Flatten::~Flatten() = default;

void
Flatten::operator()(bool const flatten)
{
  m_impl.reset(flatten ? new Impl() : nullptr);
}

void
Flatten::operator()(float     * const data,
                    std::size_t const size)
{
  if (m_impl && size) (*m_impl)(data, size);
}

/******************************************************************************/
