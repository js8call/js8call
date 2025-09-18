/**
 * (C) 2025 Allan Bazinet <w6baz@arrl.net> - All Rights Reserved
 **/

#include "JS8.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <complex>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <memory>
#include <mutex>
#include <numeric>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>
#include <boost/crc.hpp>
#include <boost/math/ccmath/round.hpp>
#include <boost/multi_index_container.hpp>
#include <boost/multi_index/key.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/ranked_index.hpp>
#include <fftw3.h>
#include <vendor/Eigen/Dense>
#include <QDebug>
#include "commons.h"

// A C++ conversion of the Fortran JS8 encoding and decoder function.
// Some notes on the conversion:
//
//   1. Names of variables and functions as much as possible match those
//      of the Fortran routines, for ease in cross-referencing during the
//      debug comparison phase of testing. You don't have to like them; I
//      don't like them either, frankly, but it's the reasonable approach
//      to the problem as of this writing; we can make 'em pretty later.
//
//   2. The BP decoder should be a faithful reproduction of the Fortran
//      version, albeit modified for the column-major vs. row-major
//      differences between the two languages.
//
//   3. The OSD decoder is no longer used, and the depth is now fixed at
//      2, instead of being variable 1 to 4.
//
//   4. The Fortran version didn't compute the 40% rank consistently in
//      syncjs8(); this version does. It wasn't typically off by much, but
//      it was reliably not going to be at 40%. Hopefully, this change will
//      result in more predictable first-pass candidate selection.
//
//   5. The Fortran version was very subject to Runge's phenomenon when
//      computing the baseline in baselinejs8(), and was using a ton of
//      data points below the 10% threshold for the polynomial determination.
//      Neither of these seemed to be helpful, so in contrast we're using
//      a number of Chebyshev nodes proportional to the desired polynomial
//      terms.
//
//   6. The Fortran version normalized `s1` by dividing by the median in
//      js8dec(), but did so in a naive manner, not checking for a median
//      of zero. Testing indicates that the normalization does not appear
//      to contribute to decoder yield, so it's been removed.
//
//   7. Translating array indices from the world of Fortran to that of C++
//      is no one's fun task. If you see things that aren't behaving as
//      expected, look at the Fortran code and compare the array indexing;
//      would not be surprised in the least to have off-by-one errors here.

/******************************************************************************/
// Compilation Utilities
/******************************************************************************/

namespace
{
    // Full-range cosine function using symmetries of cos(x). std::cos
    // isn't constexpr until C++20, and we're targeting C++17 at the
    // moment. We only use this function during compilation; std::cos
    // is the better choice at runtime. Once we move to requiring a
    // C++20 compiler, we can just use std::cos.

    constexpr auto
    cos(double x)
    {
        constexpr auto RAD_360 = M_PI * 2;
        constexpr auto RAD_180 = M_PI;
        constexpr auto RAD_90  = M_PI_2;

        // Polynomial approximation of cos(x) for x in [0, RAD_90],
        // Accuracy here in theory is 1e-18, but double precision
        // itself is only 1-e16, so within the domain of doubles,
        // this should be extremely accurate.

        constexpr auto cos = [](double const x)
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
}

/******************************************************************************/
// Constants
/******************************************************************************/

namespace
{
    /* COMMON PARAMETERS */

    // !Common
    //
    // parameter (KK=87)                     !Information bits (75 + CRC12)
    // parameter (ND=58)                     !Data symbols
    // parameter (NS=21)                     !Sync symbols (3 @ Costas 7x7)
    // parameter (NN=NS+ND)                  !Total channel symbols (79)
    // parameter (ASYNCMIN=1.5)              !Minimum Sync
    // parameter (NFSRCH=5)                  !Search frequency range in Hz (i.e., +/- 2.5 Hz)
    // parameter (NMAXCAND=300)              !Maximum number of candidate signals

    // Parameter	Value	Description
    // KK	87	Number of information bits (75 message bits + 12 CRC bits).
    // ND	58	Number of data symbols in the JS8 transmission.
    // NS	21	Number of synchronization symbols (3 Costas arrays of size 7).
    // NN	79	Total number of channel symbols (NN = NS + ND).
    // ASYNCMIN	1.5	Minimum sync value for successful decoding.
    // NFSRCH	5	Search frequency range in Hz (±2.5 Hz).
    // NMAXCAND	300	Maximum number of candidate signals.

    constexpr int         N        = 174;    // Total bits
    constexpr int         K        = 87;     // Message bits
    constexpr int         M        = N - K;  // Check bits
    constexpr int         KK       = 87;       // Information bits (75 + CRC12)
    constexpr int         ND       = 58;       // Data symbols
    constexpr int         NS       = 21;       // Sync symbols (3 @ Costas 7x7)
    constexpr int         NN       = NS + ND;  // Total channel symbols (79)
    constexpr float       ASYNCMIN = 1.5f;     // Minimum sync
    constexpr int         NFSRCH   = 5;        // Search frequency range in Hz (i.e., +/- 2.5 Hz)
    constexpr std::size_t NMAXCAND = 300;      // Maxiumum number of candidate signals
    constexpr int         NFILT    = 1400;  // Filter length
    constexpr int         NROWS    = 8;
    constexpr int         NFOS     = 2;
    constexpr int         NSSY     = 4;
    constexpr int         NP       = 3200;
    constexpr int         NP2      = 2812;
    constexpr float       TAU      = 2.0f * M_PI;
    constexpr auto        ZERO     = std::complex<float>{0.0f, 0.0f};

    // Key for the constants that follow:
    //
    //   NSUBMODE - ID of the submode
    //   NCOSTAS  - Which JS8 Costas Arrays to use
    //   NSPS     - Number of samples per second
    //   NTXDUR   - Duration of the transmission in seconds.
    //   NDOWNSPS - Number of samples per symbol after downsampling.
    //   NDD      - Parameter used in waveform tapering and related calculations. XXX
    //   JZ       - Range of symbol offsets considered during decoding.
    //   ASTART   - Start delay in seconds for decoding.
    //   BASESUB  - XXX
    //   NMAX     - Samples in input wave
    //   NSTEP    - Rough time-sync step size
    //   NHSYM    - Number of symbol spectra (1/4-sym steps)
    //   NDOW     - Downsample factor to 32 samples per symbol
    //   NQSYMBOL - Downsample factor of a quarter symbol

    /* A MODE DECODER */

    struct ModeA
    {
        // Static constants
        inline static constexpr int   NSUBMODE = 0;
        inline static constexpr auto  NCOSTAS  = JS8::Costas::Type::ORIGINAL;
        inline static constexpr int   NSPS     = JS8A_SYMBOL_SAMPLES;
        inline static constexpr int   NTXDUR   = JS8A_TX_SECONDS;
        inline static constexpr int   NDOWNSPS = 32;
        inline static constexpr int   NDD      = 100;
        inline static constexpr int   JZ       = 62;
        inline static constexpr float ASTART   = 0.5f;
        inline static constexpr float BASESUB  = 40.0f;

        // Derived parameters
        inline static constexpr float AZ       = (12000.0f / NSPS) * 0.64f;
        inline static constexpr int   NMAX     = NTXDUR * JS8_RX_SAMPLE_RATE;
        inline static constexpr int   NFFT1    = NSPS * NFOS;
        inline static constexpr int   NSTEP    = NSPS / NSSY;
        inline static constexpr int   NHSYM    = NMAX / NSTEP - 3;
        inline static constexpr int   NDOWN    = NSPS / NDOWNSPS;
        inline static constexpr int   NQSYMBOL = NDOWNSPS / 4;
        inline static constexpr int   NDFFT1   = NSPS * NDD;
        inline static constexpr int   NDFFT2   = NDFFT1 / NDOWN;
        inline static constexpr int   NP2      = NN * NDOWNSPS;
        inline static constexpr float TSTEP    = NSTEP / 12000.0f;
        inline static constexpr int   JSTRT    = ASTART / TSTEP;
        inline static constexpr float DF       = 12000.0f / NFFT1;
    };

    /* B MODE DECODER */

    struct ModeB
    {
        // Static constants
        inline static constexpr int   NSUBMODE = 1;
        inline static constexpr auto  NCOSTAS  = JS8::Costas::Type::MODIFIED;
        inline static constexpr int   NSPS     = JS8B_SYMBOL_SAMPLES;
        inline static constexpr int   NTXDUR   = JS8B_TX_SECONDS;
        inline static constexpr int   NDOWNSPS = 20;
        inline static constexpr int   NDD      = 100;
        inline static constexpr int   JZ       = 144;
        inline static constexpr float ASTART   = 0.2f;
        inline static constexpr float BASESUB  = 39.0f;

        // Derived parameters
        inline static constexpr float AZ       = (12000.0f / NSPS) * 0.8f;
        inline static constexpr int   NMAX     = NTXDUR * JS8_RX_SAMPLE_RATE;
        inline static constexpr int   NFFT1    = NSPS * NFOS;
        inline static constexpr int   NSTEP    = NSPS / NSSY;
        inline static constexpr int   NHSYM    = NMAX / NSTEP - 3;
        inline static constexpr int   NDOWN    = NSPS / NDOWNSPS;
        inline static constexpr int   NQSYMBOL = NDOWNSPS / 4;
        inline static constexpr int   NDFFT1   = NSPS * NDD;
        inline static constexpr int   NDFFT2   = NDFFT1 / NDOWN;
        inline static constexpr int   NP2      = NN * NDOWNSPS;
        inline static constexpr float TSTEP    = NSTEP / 12000.0f;
        inline static constexpr int   JSTRT    = ASTART / TSTEP;
        inline static constexpr float DF       = 12000.0f / NFFT1;
    };

    /* C MODE DECODER */

    struct ModeC
    {
        // Static constants
        inline static constexpr int   NSUBMODE = 2;
        inline static constexpr auto  NCOSTAS  = JS8::Costas::Type::MODIFIED;
        inline static constexpr int   NSPS     = JS8C_SYMBOL_SAMPLES;
        inline static constexpr int   NTXDUR   = JS8C_TX_SECONDS;
        inline static constexpr int   NDOWNSPS = 12;
        inline static constexpr int   NDD      = 120;
        inline static constexpr int   JZ       = 172;
        inline static constexpr float ASTART   = 0.1f;
        inline static constexpr float BASESUB  = 38.0f;


        // Derived parameters
        inline static constexpr float AZ       = (12000.0f / NSPS) * 0.6f;
        inline static constexpr int   NMAX     = NTXDUR * JS8_RX_SAMPLE_RATE;
        inline static constexpr int   NFFT1    = NSPS * NFOS;
        inline static constexpr int   NSTEP    = NSPS / NSSY;
        inline static constexpr int   NHSYM    = NMAX / NSTEP - 3;
        inline static constexpr int   NDOWN    = NSPS / NDOWNSPS;
        inline static constexpr int   NQSYMBOL = NDOWNSPS / 4;
        inline static constexpr int   NDFFT1   = NSPS * NDD;
        inline static constexpr int   NDFFT2   = NDFFT1 / NDOWN;
        inline static constexpr int   NP2      = NN * NDOWNSPS;
        inline static constexpr float TSTEP    = NSTEP / 12000.0f;
        inline static constexpr int   JSTRT    = ASTART / TSTEP;
        inline static constexpr float DF       = 12000.0f / NFFT1;
    };

    /* E MODE DECODER */

    // Note that the original used 28 for NTXDUR and 90 for NDD, but the
    // corresponding C++ mainline side used 30 for NTXDUR, so for the
    // moment, we're matching that here, which seems logical at present.

    struct ModeE
    {
        // Static constants
        inline static constexpr int   NSUBMODE = 4;
        inline static constexpr auto  NCOSTAS  = JS8::Costas::Type::MODIFIED;
        inline static constexpr int   NSPS     = JS8E_SYMBOL_SAMPLES;
        inline static constexpr int   NTXDUR   = JS8E_TX_SECONDS;  // XXX was 28 in Fortran
        inline static constexpr int   NDOWNSPS = 32;
        inline static constexpr int   NDD      = 94;               // XXX was 90 in Fortran
        inline static constexpr int   JZ       = 32;
        inline static constexpr float ASTART   = 0.5f;
        inline static constexpr float BASESUB  = 42.0f;

        // Derived parameters
        inline static constexpr float AZ       = (12000.0f / NSPS) * 0.64f;
        inline static constexpr int   NMAX     = NTXDUR * JS8_RX_SAMPLE_RATE;
        inline static constexpr int   NFFT1    = NSPS * NFOS;
        inline static constexpr int   NSTEP    = NSPS / NSSY;
        inline static constexpr int   NHSYM    = NMAX / NSTEP - 3;
        inline static constexpr int   NDOWN    = NSPS / NDOWNSPS;
        inline static constexpr int   NQSYMBOL = NDOWNSPS / 4;
        inline static constexpr int   NDFFT1   = NSPS * NDD;
        inline static constexpr int   NDFFT2   = NDFFT1 / NDOWN;
        inline static constexpr int   NP2      = NN * NDOWNSPS;
        inline static constexpr float TSTEP    = NSTEP / 12000.0f;
        inline static constexpr int   JSTRT    = ASTART / TSTEP;
        inline static constexpr float DF       = 12000.0f / NFFT1;
    };

    /* I MODE DECODER */

    struct ModeI
    {
        // Static constants
        inline static constexpr int   NSUBMODE = 8;
        inline static constexpr auto  NCOSTAS  = JS8::Costas::Type::MODIFIED;
        inline static constexpr int   NSPS     = JS8I_SYMBOL_SAMPLES;
        inline static constexpr int   NTXDUR   = JS8I_TX_SECONDS;
        inline static constexpr int   NDOWNSPS = 12;
        inline static constexpr int   NDD      = 125;
        inline static constexpr int   JZ       = 250;
        inline static constexpr float ASTART   = 0.1f;
        inline static constexpr float BASESUB  = 36.0f;

        // Derived parameters
        inline static constexpr float AZ       = (12000.0f / NSPS) * 0.64f;
        inline static constexpr int   NMAX     = NTXDUR * JS8_RX_SAMPLE_RATE;
        inline static constexpr int   NFFT1    = NSPS * NFOS;
        inline static constexpr int   NSTEP    = NSPS / NSSY;
        inline static constexpr int   NHSYM    = NMAX / NSTEP - 3;
        inline static constexpr int   NDOWN    = NSPS / NDOWNSPS;
        inline static constexpr int   NQSYMBOL = NDOWNSPS / 4;
        inline static constexpr int   NDFFT1   = NSPS * NDD;
        inline static constexpr int   NDFFT2   = NDFFT1 / NDOWN;
        inline static constexpr int   NP2      = NN * NDOWNSPS;
        inline static constexpr float TSTEP    = NSTEP / 12000.0f;
        inline static constexpr int   JSTRT    = ASTART / TSTEP;
        inline static constexpr float DF       = 12000.0f / NFFT1;
    };

    // Tunable settings; degree of the polynomial used for the baseline
    // curve fit, and the percentile of the span at which to sample. In
    // general, a 5th degree polynomial and the 10th percentile should
    // be optimal.

    constexpr auto BASELINE_DEGREE =  5;
    constexpr auto BASELINE_SAMPLE = 10;

    // Define the closed range in Hz that we'll consider to be the window
    // for baseline determination.

    constexpr auto BASELINE_MIN  = 500;
    constexpr auto BASELINE_MAX = 2500;

    // We're going to do a pairwise Estrin's evaluation of the polynomial
    // coefficients, so it's critical that the degree of the polynomial is
    // odd, resulting in an even number of coefficients.

    static_assert(BASELINE_DEGREE &  1,   "Degree must be odd");
    static_assert(BASELINE_SAMPLE >= 0 &&
                  BASELINE_SAMPLE <= 100, "Sample must be a percentage");

    // Since we know the degree of the polynomial, and thus the number of
    // nodes that we're going to use, we can do all the trigonometry work
    // required to calculate the Chebyshev nodes in advance, by computing
    // them over the range [0, 1]; we can then scale these at runtime to
    // a span of any size by simple multiplication.
    //
    // Downside to this with C++17 is that std::cos() is not yet constexpr,
    // as it is in C++20, so we must provide our own implementation until
    // then.

    constexpr auto BASELINE_NODES = []()
    {
        // Down to the actual business of generating Chebyshev nodes
        // suitable for scaling; once we move to C++20 as the minimum
        // compiler, we can remove the cos() function above and instead
        // call std::cos() here, as it's required to be constexpr in
        // C++20 and above, and presumably it'll be of high quality.

        auto           nodes = std::array<double, BASELINE_DEGREE + 1>{};
        constexpr auto slice = M_PI / (2.0 * nodes.size());

        for (std::size_t i = 0; i < nodes.size(); ++i)
        {
            nodes[i] = 0.5 * (1.0 - cos(slice * (2.0 * i + 1)));
        }

        return nodes;
    }();
}

/******************************************************************************/
// Local Types
/******************************************************************************/

namespace
{
    // Accumulation of rounding errors in IEEE 754 values can be a problem
    // when summing large numbers of small values; a Kahan summation class
    // by which to compensate for them.
    //
    // Fortran, or at least, gfortran, will use this technique under the
    // covers in various scenarios. While it'd be reasonable to expect it
    // to be used in sum(), that's typically not the case.
    //
    // However, for example, it'll use it here for the value that goes into
    // win(i), and naive summation in C++ will as a result not produce the
    // same values without using compensation.
    //
    //   subroutine nuttal_window(win,n)
    //     real win(n)
    //     pi=4.0*atan(1.0)
    //     a0=0.3635819
    //     a1=-0.4891775;
    //     a2=0.1365995;
    //     a3=-0.0106411;
    //     do i=1,n
    //         win(i)=a0+a1*cos(2*pi*(i-1)/(n))+ &
    //             a2*cos(4*pi*(i-1)/(n))+ &
    //             a3*cos(6*pi*(i-1)/(n))
    //     enddo
    //     return
    //   end subroutine nuttal_window

    template <std::floating_point T>
    class KahanSum
    {
        T m_sum;           // Accumulated sum
        T m_compensation;  // Compensation for lost low-order bits

    public:

        KahanSum(T sum = T(0))
        : m_sum(sum)
        , m_compensation(T(0))
        {}

        KahanSum &
        operator=(T const sum)
        {
            m_sum          = sum;
            m_compensation = T(0);

            return *this;
        }

        KahanSum &
        operator+=(T const value)
        {
            T const y = value - m_compensation;   // Correct the value
            T const t = m_sum + y;                // Perform the sum

            m_compensation = (t - m_sum) - y;     // Update compensation
            m_sum          =  t;                  // Update the sum

            return *this;
        }

        operator T() const { return m_sum; }
    };

    // Management of dynamic FFTW plan storage.

    class FFTWPlanManager
    {
    public:

        enum class Type
        {
            DS,
            BB,
            CF,
            CB,
            SD,
            CS,
            count
        };

        // Disallow copying and moving

        FFTWPlanManager            (FFTWPlanManager const &) = delete;
        FFTWPlanManager & operator=(FFTWPlanManager const &) = delete;
        FFTWPlanManager            (FFTWPlanManager      &&) = delete;
        FFTWPlanManager & operator=(FFTWPlanManager      &&) = delete;

        // Constructor

        FFTWPlanManager()
        {
            m_plans.fill(nullptr);
        }

        // Destructor

        ~FFTWPlanManager()
        {
            std::lock_guard<std::mutex> lock(fftw_mutex);

            for (auto & plan : m_plans)
            {
                if (plan) fftwf_destroy_plan(plan);
            }
        }

        // Accessor

        fftwf_plan const &
        operator[](Type const type) const noexcept
        {
            return m_plans[static_cast<std::size_t>(type)];
        }

        // Manipulator

        fftwf_plan &
        operator[](Type const type) noexcept
        {
            return m_plans[static_cast<std::size_t>(type)];
        }

        // Iteration support

        auto begin()       noexcept { return m_plans.begin(); }
        auto end()         noexcept { return m_plans.end();   }
        auto begin() const noexcept { return m_plans.begin(); }
        auto end()   const noexcept { return m_plans.end();   }

    private:

        // Data members

        std::array<fftwf_plan, static_cast<std::size_t>(Type::count)> m_plans;
    };

    // Encapsulates the first-order search results provided by syncjs8().

    struct Sync
    {
        float freq;
        float step;
        float sync;

        // Constructor for convenience.

        Sync(float const freq,
             float const step,
             float const sync)
        : freq(freq)
        , step(step)
        , sync(sync)
        {}
    };

    // Tag structs so that we can refer to multi index container indices
    // by a descriptive tag instead of by the index of the index. These
    // don't need to be anything but a name.

    namespace Tag
    {
        struct Freq {};
        struct Rank {};
        struct Sync {};
    }

    // Container indexing Sync objects in useful ways, used by syncjs8().

    namespace MI    = boost::multi_index;
    using SyncIndex = MI::multi_index_container
    <
        Sync,
        MI::indexed_by
        <
            MI::ordered_non_unique<
                MI::tag<Tag::Freq>,
                MI::key<&Sync::freq>
            >,
            MI::ranked_non_unique<
                MI::tag<Tag::Rank>,
                MI::key<&Sync::sync>
            >,
            MI::ordered_non_unique<
                MI::tag<Tag::Sync>,
                MI::key<&Sync::sync>,
                std::greater<>
            >
        >
    >;

    // Represents a decoded message, i.e., the 3-bit message type
    // and the 12 bytes that result from decoding a message.

    class Decode
    {
    public:

        int         type;
        std::string data;

        Decode(int         type,
               std::string data)
        : type(type)
        , data(std::move(data))
        {}

        bool operator==(Decode const &) const noexcept = default;

        struct Hash
        {
            std::size_t
            operator()(Decode const & decode) const noexcept
            {
                std::size_t const h1 = std::hash<int>{}(decode.type);
                std::size_t const h2 = std::hash<std::string>{}(decode.data);
                return h1 ^ (h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2));
            }
        };

        using Map = std::unordered_map<Decode, int, Hash>;
    };
}

/******************************************************************************/
// Belief Propagation Decoder
/******************************************************************************/

namespace
{
    constexpr int BP_MAX_ROWS       = 7;  // Max rows per column in Nm
    constexpr int BP_MAX_CHECKS     = 3;  // Max checks per bit in Mn
    constexpr int BP_MAX_ITERATIONS = 30; // Max iterations in BP decoder

    constexpr std::array<std::array<int, BP_MAX_CHECKS>, N> Mn =
    {{
        { 0, 24, 68}, { 1,  4, 72}, { 2, 31, 67}, { 3, 50, 60}, { 5, 62, 69}, { 6, 32, 78},
        { 7, 49, 85}, { 8, 36, 42}, { 9, 40, 64}, {10, 13, 63}, {11, 74, 76}, {12, 22, 80},
        {14, 15, 81}, {16, 55, 65}, {17, 52, 59}, {18, 30, 51}, {19, 66, 83}, {20, 28, 71},
        {21, 23, 43}, {25, 34, 75}, {26, 35, 37}, {27, 39, 41}, {29, 53, 54}, {33, 48, 86},
        {38, 56, 57}, {44, 73, 82}, {45, 61, 79}, {46, 47, 84}, {58, 70, 77}, { 0, 49, 52},
        { 1, 46, 83}, { 2, 24, 78}, { 3,  5, 13}, { 4,  6, 79}, { 7, 33, 54}, { 8, 35, 68},
        { 9, 42, 82}, {10, 22, 73}, {11, 16, 43}, {12, 56, 75}, {14, 26, 55}, {15, 27, 28},
        {17, 18, 58}, {19, 39, 62}, {20, 34, 51}, {21, 53, 63}, {23, 61, 77}, {25, 31, 76},
        {29, 71, 84}, {30, 64, 86}, {32, 38, 50}, {36, 47, 74}, {37, 69, 70}, {40, 41, 67},
        {44, 66, 85}, {45, 80, 81}, {48, 65, 72}, {57, 59, 65}, {60, 64, 84}, { 0, 13, 20},
        { 1, 12, 58}, { 2, 66, 81}, { 3, 31, 72}, { 4, 35, 53}, { 5, 42, 45}, { 6, 27, 74},
        { 7, 32, 70}, { 8, 48, 75}, { 9, 57, 63}, {10, 47, 67}, {11, 18, 44}, {14, 49, 60},
        {15, 21, 25}, {16, 71, 79}, {17, 39, 54}, {19, 34, 50}, {22, 24, 33}, {23, 62, 86},
        {26, 38, 73}, {28, 77, 82}, {29, 69, 76}, {30, 68, 83}, {21, 36, 85}, {37, 40, 80},
        {41, 43, 56}, {46, 52, 61}, {51, 55, 78}, {59, 74, 80}, { 0, 38, 76}, { 1, 15, 40},
        { 2, 30, 53}, { 3, 35, 77}, { 4, 44, 64}, { 5, 56, 84}, { 6, 13, 48}, { 7, 20, 45},
        { 8, 14, 71}, { 9, 19, 61}, {10, 16, 70}, {11, 33, 46}, {12, 67, 85}, {17, 22, 42},
        {18, 63, 72}, {23, 47, 78}, {24, 69, 82}, {25, 79, 86}, {26, 31, 39}, {27, 55, 68},
        {28, 62, 65}, {29, 41, 49}, {32, 36, 81}, {34, 59, 73}, {37, 54, 83}, {43, 51, 60},
        {50, 52, 71}, {57, 58, 66}, {46, 55, 75}, { 0, 18, 36}, { 1, 60, 74}, { 2,  7, 65},
        { 3, 59, 83}, { 4, 33, 38}, { 5, 25, 52}, { 6, 31, 56}, { 8, 51, 66}, { 9, 11, 14},
        {10, 50, 68}, {12, 13, 64}, {15, 30, 42}, {16, 19, 35}, {17, 79, 85}, {20, 47, 58},
        {21, 39, 45}, {22, 32, 61}, {23, 29, 73}, {24, 41, 63}, {26, 48, 84}, {27, 37, 72},
        {28, 43, 80}, {34, 67, 69}, {40, 62, 75}, {44, 48, 70}, {49, 57, 86}, {47, 53, 82},
        {12, 54, 78}, {76, 77, 81}, { 0,  1, 23}, { 2,  5, 74}, { 3, 55, 86}, { 4, 43, 52},
        { 6, 49, 82}, { 7,  9, 27}, { 8, 54, 61}, {10, 28, 66}, {11, 32, 39}, {13, 15, 19},
        {14, 34, 72}, {16, 30, 38}, {17, 35, 56}, {18, 45, 75}, {20, 41, 83}, {21, 33, 58},
        {22, 25, 60}, {24, 59, 64}, {26, 63, 79}, {29, 36, 65}, {31, 44, 71}, {37, 50, 85},
        {40, 76, 78}, {42, 55, 67}, {46, 73, 81}, {39, 51, 77}, {53, 60, 70}, {45, 57, 68}
    }};

    struct CheckNode
    {
        int                    valid_neighbors;
        std::array<int, BP_MAX_ROWS> neighbors;
    };

    constexpr std::array<CheckNode, M> Nm =
    {{
        {6, { 0, 29, 59,  88, 117, 146,   0}}, {6, { 1, 30, 60,  89, 118, 146,   0}}, {6, { 2, 31, 61,  90, 119, 147, 0}},
        {6, { 3, 32, 62,  91, 120, 148,   0}}, {6, { 1, 33, 63,  92, 121, 149,   0}}, {6, { 4, 32, 64,  93, 122, 147, 0}},
        {6, { 5, 33, 65,  94, 123, 150,   0}}, {6, { 6, 34, 66,  95, 119, 151,   0}}, {6, { 7, 35, 67,  96, 124, 152, 0}},
        {6, { 8, 36, 68,  97, 125, 151,   0}}, {6, { 9, 37, 69,  98, 126, 153,   0}}, {6, {10, 38, 70,  99, 125, 154, 0}},
        {6, {11, 39, 60, 100, 127, 144,   0}}, {6, { 9, 32, 59,  94, 127, 155,   0}}, {6, {12, 40, 71,  96, 125, 156, 0}},
        {6, {12, 41, 72,  89, 128, 155,   0}}, {6, {13, 38, 73,  98, 129, 157,   0}}, {6, {14, 42, 74, 101, 130, 158, 0}},
        {6, {15, 42, 70, 102, 117, 159,   0}}, {6, {16, 43, 75,  97, 129, 155,   0}}, {6, {17, 44, 59,  95, 131, 160, 0}},
        {6, {18, 45, 72,  82, 132, 161,   0}}, {6, {11, 37, 76, 101, 133, 162,   0}}, {6, {18, 46, 77, 103, 134, 146, 0}},
        {6, { 0, 31, 76, 104, 135, 163,   0}}, {6, {19, 47, 72, 105, 122, 162,   0}}, {6, {20, 40, 78, 106, 136, 164, 0}},
        {6, {21, 41, 65, 107, 137, 151,   0}}, {6, {17, 41, 79, 108, 138, 153,   0}}, {6, {22, 48, 80, 109, 134, 165, 0}},
        {6, {15, 49, 81,  90, 128, 157,   0}}, {6,  {2, 47, 62, 106, 123, 166,   0}}, {6, { 5, 50, 66, 110, 133, 154, 0}},
        {6, {23, 34, 76,  99, 121, 161,   0}}, {6, {19, 44, 75, 111, 139, 156,   0}}, {6, {20, 35, 63,  91, 129, 158, 0}},
        {6, { 7, 51, 82, 110, 117, 165,   0}}, {6, {20, 52, 83, 112, 137, 167,   0}}, {6, {24, 50, 78,  88, 121, 157, 0}},
        {7, {21, 43, 74, 106, 132, 154, 171}}, {6, { 8, 53, 83,  89, 140, 168,   0}}, {6, {21, 53, 84, 109, 135, 160, 0}},
        {6, { 7, 36, 64, 101, 128, 169,   0}}, {6, {18, 38, 84, 113, 138, 149,   0}}, {6, {25, 54, 70,  92, 141, 166, 0}},
        {7, {26, 55, 64,  95, 132, 159, 173}}, {6, {27, 30, 85,  99, 116, 170,   0}}, {6, {27, 51, 69, 103, 131, 143, 0}},
        {6, {23, 56, 67,  94, 136, 141,   0}}, {6, {6,  29, 71, 109, 142, 150,   0}}, {6, { 3, 50, 75, 114, 126, 167, 0}},
        {6, {15, 44, 86, 113, 124, 171,   0}}, {6, {14, 29, 85, 114, 122, 149,   0}}, {6, {22, 45, 63,  90, 143, 172, 0}},
        {6, {22, 34, 74, 112, 144, 152,   0}}, {7, {13, 40, 86, 107, 116, 148, 169}}, {6, {24, 39, 84,  93, 123, 158, 0}},
        {6, {24, 57, 68, 115, 142, 173,   0}}, {6, {28, 42, 60, 115, 131, 161,   0}}, {6, {14, 57, 87, 111, 120, 163, 0}},
        {7, { 3, 58, 71, 113, 118, 162, 172}}, {6, {26, 46, 85,  97, 133, 152,   0}}, {5, { 4, 43, 77, 108, 140,   0, 0}},
        {6, { 9, 45, 68, 102, 135, 164,   0}}, {6, { 8, 49, 58,  92, 127, 163,   0}}, {6, {13, 56, 57, 108, 119, 165, 0}},
        {6, {16, 54, 61, 115, 124, 153,   0}}, {6, { 2, 53, 69, 100, 139, 169,   0}}, {6, { 0, 35, 81, 107, 126, 173, 0}},
        {5, { 4, 52, 80, 104, 139,   0,   0}}, {6, {28, 52, 66,  98, 141, 172,   0}}, {6, {17, 48, 73,  96, 114, 166, 0}},
        {6, { 1, 56, 62, 102, 137, 156,   0}}, {6, {25, 37, 78, 111, 134, 170,   0}}, {6, {10, 51, 65,  87, 118, 147, 0}},
        {6, {19, 39, 67, 116, 140, 159,   0}}, {6, {10, 47, 80,  88, 145, 168,   0}}, {6, {28, 46, 79,  91, 145, 171, 0}},
        {6, { 5, 31, 86, 103, 144, 168,   0}}, {6, {26, 33, 73, 105, 130, 164,   0}}, {5, {11, 55, 83,  87, 138,   0, 0}},
        {6, {12, 55, 61, 110, 145, 170,   0}}, {6, {25, 36, 79, 104, 143, 150,   0}}, {6, {16, 30, 81, 112, 120, 160, 0}},
        {5, {27, 48, 58,  93, 136,   0,   0}}, {6, { 6, 54, 82, 100, 130, 167,   0}}, {6, {23, 49, 77, 105, 142, 148, 0}}
    }};

    // Belief Propagation Decoder

    int
    bpdecode174(std::array<float, N> const & llr,
                std::array<int8_t, K>      & decoded,
                std::array<int8_t, N>      & cw)
    {
        // Initialize messages and variables
        std::array<std::array<float, BP_MAX_CHECKS>, N> tov     = {}; // Messages to variable nodes
        std::array<std::array<float, BP_MAX_ROWS>,   M> toc     = {}; // Messages to check nodes
        std::array<std::array<float, BP_MAX_ROWS>  , M> tanhtoc = {}; // Tanh of messages

        std::array<float, N> zn   = {}; // Bit log likelihood ratios
        std::array<int,   M> synd = {}; // Syndrome for checks

        int ncnt   = 0;
        int nclast = 0;

        // Initialize toc (messages from bits to checks)
        for (int i = 0; i < M; ++i) {
            for (int j = 0; j < Nm[i].valid_neighbors; ++j) {
                toc[i][j] = llr[Nm[i].neighbors[j]];
            }
        }

        // Iterative decoding
        for (int iter = 0; iter <= BP_MAX_ITERATIONS; ++iter) {
            // Update bit log likelihood ratios
            for (int i = 0; i < N; ++i) {
                zn[i] = llr[i] + std::accumulate(tov[i].begin(), tov[i].begin() + BP_MAX_CHECKS, 0.0f);
            }

            // Check if we have a valid codeword
            for (int i = 0; i < N; ++i) cw[i] = zn[i] > 0 ? 1 : 0;

            int ncheck = 0;
            for (int i = 0; i < M; ++i) {
                synd[i] = 0;
                for (int j = 0; j < Nm[i].valid_neighbors; ++j) {
                    synd[i] += cw[Nm[i].neighbors[j]];
                }
                if (synd[i] % 2 != 0) ++ncheck;
            }

            if (ncheck == 0)
            {
                // Extract decoded bits (last N-M bits of codeword)
                std::copy(cw.begin() + M, cw.end(), decoded.begin());

                // Count errors
                int nerr = 0;
                for (int i = 0; i < N; ++i) {
                    if ((2 * cw[i] - 1) * llr[i] < 0.0f) {
                        ++nerr;
                    }
                }

                return nerr;
            }

            // Early stopping criterion
            if (iter > 0) {
                int nd = ncheck - nclast;
                ncnt = (nd < 0) ? 0 : ncnt + 1;
                if (ncnt >= 5 && iter >= 10 && ncheck > 15) {
                    return -1;
                }
            }
            nclast = ncheck;

            // Send messages from bits to check nodes
            for (int i = 0; i < M; ++i) {
                for (int j = 0; j < Nm[i].valid_neighbors; ++j) {
                    int ibj = Nm[i].neighbors[j];
                    toc[i][j] = zn[ibj];
                    for (int k = 0; k < BP_MAX_CHECKS; ++k) {
                        if (Mn[ibj][k] == i) {
                            toc[i][j] -= tov[ibj][k];
                        }
                    }
                }
            }

            // Send messages from check nodes to variable nodes
            for (int i = 0; i < M; ++i) {
                for (int j = 0; j < 7; ++j) { // Fixed range [0, 7) to match Fortran's 1:7, could be nrw[j], or 7 logically
                    tanhtoc[i][j] = std::tanh(-toc[i][j] / 2.0f);
                }
            }

            for (int i = 0; i < N; ++i) {
                for (int j = 0; j < BP_MAX_CHECKS; ++j) {
                    int ichk = Mn[i][j];
                    if (ichk >= 0) {
                        float Tmn = 1.0f;
                        for (int k = 0; k < Nm[ichk].valid_neighbors; ++k) {
                            if (Nm[ichk].neighbors[k] != i) {
                                Tmn *= tanhtoc[ichk][k];
                            }
                        }
                        tov[i][j] = 2.0f * std::atanh(-Tmn);
                    }
                }
            }
        }

        return -1; // Decoding failed
    }
}

/******************************************************************************/
// Local Routines
/******************************************************************************/

namespace
{
    constexpr std::string_view alphabet = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz-+";

    static_assert(alphabet.size() == 64);

    // Function that either translates valid JS8 message characters to their
    // corresponding 6-bit word value, or throws. This will end up doing a
    // direct index operation into a 256-byte table, the creation of which
    // must be constexpr under C++17.

    constexpr auto alphabetWord = []()
    {
        constexpr std::uint8_t invalid = 0xff;

        constexpr auto words = []()
        {
            std::array<std::uint8_t, 256> words{};

            for (auto & word : words) word = invalid;

            for (std::size_t i = 0; i < alphabet.size(); ++i)
            {
                words[static_cast<std::uint8_t>(alphabet[i])] = static_cast<std::uint8_t>(i);
            }

            return words;
        }();

        return [words](char const value)
        {
            if (auto const word  = words[value];
                           word != invalid)
            {
                return word;
            }

            throw std::runtime_error("Invalid character in message");
        };
    }();

    // Sanity check key bounds of the 6-bit encoding table.

    static_assert(alphabetWord('0') == 0);
    static_assert(alphabetWord('A') == 10);
    static_assert(alphabetWord('a') == 36);
    static_assert(alphabetWord('-') == 62);
    static_assert(alphabetWord('+') == 63);

    template <typename T>
    std::uint16_t
    CRC12(T const & range)
    {
        return boost::augmented_crc<12, 0xc06>(range.data(),
                                               range.size()) ^ 42;
    }

    bool
    checkCRC12(std::array<std::int8_t, KK> const & decoded)
    {
        std::array<uint8_t, 11> bits = {};

        for (std::size_t i = 0; i < decoded.size(); ++i)
        {
            if (decoded[i]) bits[i / 8] |= (1 << (7 - (i % 8)));
        }

        // Extract the received CRC-12.

        uint16_t crc = (static_cast<uint16_t>(bits[9] & 0x1F) << 7) |
                       (static_cast<uint16_t>(bits[10])       >> 1);

        // Clear bits that correspond to the CRC in the last bytes.

        bits[9] &= 0xE0;
        bits[10] = 0x00;

        // Compute CRC and indicate if we have a match.

        return crc == CRC12(bits);
    }

    std::string
    extractmessage174(std::array<int8_t, KK> const & decoded)
    {
        std::string message;

        // Ensure received CRC matches computed CRC.

        if (checkCRC12(decoded))
        {
            message.reserve(12);

            // Decode the message from the 72 data bits

            std::array<uint8_t, 12> words;

            for (std::size_t i = 0; i < 12; ++i)
            {
                words[i] = (decoded[i * 6 + 0] << 5) |
                           (decoded[i * 6 + 1] << 4) |
                           (decoded[i * 6 + 2] << 3) |
                           (decoded[i * 6 + 3] << 2) |
                           (decoded[i * 6 + 4] << 1) |
                           (decoded[i * 6 + 5] << 0);
            }

            // Map 6-bit words to the alphabet

            for (auto const  word : words) message += alphabet[word];
        }

        return message;
    }

    // Parity matrix for JS8 message generation.
    //
    // This should be 952 bytes in size; to store an 87x87 matrix of bits,
    // you need 7569 bits, which requires 119 64-bit values, or 952 bytes.
    //
    // Background here is that this is a low-density parity check code (LDPC),
    // generated using the PEG algorithm. In short, true values in a row i of
    // the matrix define which of the 87 message bits must be summed, modulo
    // 2, to produce the ith parity check bit. Decent references on this are:
    //
    //   1. https://wsjt.sourceforge.io/FT4_FT8_QEX.pdf
    //   2. https://inference.org.uk/mackay/PEG_ECC.html
    //   3. https://github.com/Lcrypto/classic-PEG-
    //
    // The data used was harvested from the original 'ldpc_174_87_params.f90',
    // but you'll note that the rows have been reordered here, because this
    // isn't Fortran; C++ is row-major, not column-major.

    constexpr auto parity = []()
    {
        constexpr std::size_t Rows = 87;
        constexpr std::size_t Cols = 87;

        using                 ElementType = std::uint64_t;
        constexpr std::size_t ElementSize = std::numeric_limits<ElementType>::digits;

        constexpr auto matrix = []()
        {
            constexpr std::array<std::string_view, Rows> Data =
            {
                "23bba830e23b6b6f50982e", "1f8e55da218c5df3309052", "ca7b3217cd92bd59a5ae20",
                "56f78313537d0f4382964e", "6be396b5e2e819e373340c", "293548a138858328af4210",
                "cb6c6afcdc28bb3f7c6e86", "3f2a86f5c5bd225c961150", "849dd2d63673481860f62c",
                "56cdaec6e7ae14b43feeee", "04ef5cfa3766ba778f45a4", "c525ae4bd4f627320a3974",
                "41fd9520b2e4abeb2f989c", "7fb36c24085a34d8c1dbc4", "40fc3e44bb7d2bb2756e44",
                "d38ab0a1d2e52a8ec3bc76", "3d0f929ef3949bd84d4734", "45d3814f504064f80549ae",
                "f14dbf263825d0bd04b05e", "db714f8f64e8ac7af1a76e", "8d0274de71e7c1a8055eb0",
                "51f81573dd4049b082de14", "d8f937f31822e57c562370", "b6537f417e61d1a7085336",
                "ecbd7c73b9cd34c3720c8a", "3d188ea477f6fa41317a4e", "1ac4672b549cd6dba79bcc",
                "a377253773ea678367c3f6", "0dbd816fba1543f721dc72", "ca4186dd44c3121565cf5c",
                "29c29dba9c545e267762fe", "1616d78018d0b4745ca0f2", "fe37802941d66dde02b99c",
                "a9fa8e50bcb032c85e3304", "83f640f1a48a8ebc0443ea", "3776af54ccfbae916afde6",
                "a8fc906976c35669e79ce0", "f08a91fb2e1f78290619a8", "cc9da55fe046d0cb3a770c",
                "d36d662a69ae24b74dcbd8", "40907b01280f03c0323946", "d037db825175d851f3af00",
                "1bf1490607c54032660ede", "0af7723161ec223080be86", "eca9afa0f6b01d92305edc",
                "7a8dec79a51e8ac5388022", "9059dfa2bb20ef7ef73ad4", "6abb212d9739dfc02580f2",
                "f6ad4824b87c80ebfce466", "d747bfc5fd65ef70fbd9bc", "612f63acc025b6ab476f7c",
                "05209a0abb530b9e7e34b0", "45b7ab6242b77474d9f11a", "6c280d2a0523d9c4bc5946",
                "f1627701a2d692fd9449e6", "8d9071b7e7a6a2eed6965e", "bf4f56e073271f6ab4bf80",
                "c0fc3ec4fb7d2bb2756644", "57da6d13cb96a7689b2790", "a9fa2eefa6f8796a355772",
                "164cc861bdd803c547f2ac", "cc6de59755420925f90ed2", "a0c0033a52ab6299802fd2",
                "b274db8abd3c6f396ea356", "97d4169cb33e7435718d90", "81cfc6f18c35b1e1f17114",
                "481a2a0df8a23583f82d6c", "081c29a10d468ccdbcecb6", "2c4142bf42b01e71076acc",
                "a6573f3dc8b16c9d19f746", "c87af9a5d5206abca532a8", "012dee2198eba82b19a1da",
                "b1ca4ea2e3d173bad4379c", "b33ec97be83ce413f9acc8", "5b0f7742bca86b8012609a",
                "37d8e0af9258b9e8c5f9b2", "35ad3fb0faeb5f1b0c30dc", "6114e08483043fd3f38a8a",
                "cd921fdf59e882683763f6", "95e45ecd0135aca9d6e6ae", "2e547dd7a05f6597aac516",
                "14cd0f642fc0c5fe3a65ca", "3a0a1dfd7eee29c2e827e0", "c8b5dffc335095dcdcaf2a",
                "3dd01a59d86310743ec752", "8abdb889efbe39a510a118", "3f231f212055371cf3e2a2"
            };

            constexpr std::size_t                 Total = (Rows * Cols + ElementSize - 1);
            constexpr std::size_t                 Count = Total / ElementSize;
            constexpr std::array<std::uint8_t, 4> Masks = {0x8, 0x4, 0x2, 0x1};

            std::array<ElementType, Count> data{};

            for (std::size_t row = 0; row < Rows; ++row)
            {
                std::size_t col = 0;

                for (auto const c : Data[row])
                {
                    std::uint8_t const value = (c >= '0' && c <= '9') ? c - '0' :
                                               (c >= 'a' && c <= 'f') ? c - 'a' + 10 :
                                               (c >= 'A' && c <= 'F') ? c - 'A' + 10 : throw "Invalid hex";

                    for (auto const mask : Masks)
                    {
                        if (col >= Cols) break;
                        if (value & mask)
                        {
                            auto const index = row * Cols + col;
                            data[index / ElementSize] |= (ElementType(1) << (index % ElementSize));
                        }
                        ++col;
                    }
                }
            }
            return data;
        }();

        return [matrix](std::size_t const row,
                        std::size_t const col)
        {
            auto const index = row * Cols + col;
            return (matrix[index / ElementSize] >>
                          (index % ElementSize)) & 1;
        };
    }();
}

/******************************************************************************/
// DecodeMode Template Class
/******************************************************************************/

// Mode-parameterized decode class.

namespace
{
    template <typename Mode>
    class DecodeMode
    {
        // Data members

        std::array<float, Mode::NFFT1>                                                nuttal;
        std::array<std::array<std::array<std::complex<float>, Mode::NDOWNSPS>, 7>, 3> csyncs;
        alignas(64) std::array<std::complex<float>, Mode::NDOWNSPS>                   csymb;
        alignas(64) std::array<std::complex<float>, Mode::NMAX>                       filter;
        alignas(64) std::array<std::complex<float>, Mode::NMAX>                       cfilt;
        alignas(64) std::array<std::complex<float>, Mode::NDFFT1 / 2 + 1>             ds_cx;
        alignas(64) std::array<std::complex<float>, Mode::NFFT1  / 2 + 1>             sd;
        alignas(64) std::array<std::complex<float>, NP>                               cd0;
        std::array<float, Mode::NMAX>                                                 dd;
        std::array<std::array<float, Mode::NHSYM>, Mode::NSPS>                        s;
        std::array<float, Mode::NSPS>                                                 savg;
        FFTWPlanManager                                                               plans;
        SyncIndex                                                                     sync;

        using Plan = FFTWPlanManager::Type;

        static constexpr auto Costas = JS8::Costas::array(Mode::NCOSTAS);

        // Fore and aft tapers to reduce spectral leakage during the
        // downsampling process. We can compute these at compile time.

        static constexpr auto Taper = []
        {
            std::array<std::array<float, Mode::NDD + 1>, 2> taper{};

            for (size_t i = 0; i <= Mode::NDD; ++i)
            {
                float const value = 0.5f * (1.0f + cos(i * M_PI / Mode::NDD));

                taper[1][            i] = value; // TailTaper (original taper)
                taper[0][Mode::NDD - i] = value; // HeadTaper (reversed taper)
            }

            return taper;
        }();

        // Baseline computation support.

        using Points       = Eigen::Matrix<double, BASELINE_NODES.size(), 2>;
        using Vandermonde  = Eigen::Matrix<double, BASELINE_NODES.size(),
                                                   BASELINE_NODES.size()>;
        using Coefficients = Eigen::Vector<double, BASELINE_NODES.size()>;

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
            return [this]<Eigen::Index... I>(float const x,
                                             std::integer_sequence<Eigen::Index, I...>)
            {
                auto baseline = 0.0;
                auto exponent = 1.0;

                ((baseline += (c[I * 2] + c[I * 2 + 1] * x) * exponent, exponent *= x * x), ...);

                return static_cast<float>(baseline);
            }(x, std::make_integer_sequence<Eigen::Index,
                                            Coefficients::SizeAtCompileTime / 2>{});
        }

        std::optional<Decode>
        js8dec(bool          const syncStats,
               bool          const lsubtract,
               float             & f1,
               float             & xdt,
               int               & nharderrors,
               float             & xsnr,
               JS8::Event::Emitter emitEvent)
        {
            constexpr float FR  = 12000.0f / Mode::NFFT1;  // Frequency resolution
            constexpr float FS2 = 12000.0f / Mode::NDOWN;
            constexpr float DT2 = 1.0f     / FS2;

            auto  const index        = static_cast<int>(std::round(f1 / FR)); // Closest index
            float const scaled_value = 0.1f * (savg[index] - Mode::BASESUB);  // Adjust and scale
            float const xbase        = std::pow(10.0f, scaled_value);         // Convert to linear scale

            float delfbest = 0.0f;
            int   ibest    = 0;

            // Downsample the signal and prepare for processing.

            js8_downsample(f1);

            // Initial guess for the start of the signal.

            int   i0   = static_cast<int>(std::round((xdt + Mode::ASTART) * FS2));
            float smax = 0.0f;

            // Search for the best synchronization offset.

            for (int idt  = i0 - Mode::NQSYMBOL;
                     idt <= i0 + Mode::NQSYMBOL;
                   ++idt)
            {
                float const sync = syncjs8d(idt, 0.0f);

                if (sync > smax) {
                    smax = sync;
                    ibest = idt;
                }
            }

            // Improved estimate for DT.

            float const xdt2 = ibest * DT2;

            // Fine frequency synchronization

            i0   = static_cast<int>(std::round(xdt2 * FS2));
            smax = 0.0f;

            for (int ifr  = -NFSRCH;
                     ifr <=  NFSRCH;
                   ++ifr)
            {
                float const delf = ifr * 0.5f;
                float const sync = syncjs8d(i0, delf);

                if (sync > smax) {
                    smax     = sync;
                    delfbest = delf;
                }
            }

            // Frequency tweaking.

            float               const dphi  = -delfbest * ((2.0f * M_PI) / FS2); // Phase increment
            std::complex<float> const wstep = std::polar(1.0f, dphi);            // Step for phase rotation
            std::complex<float>       w     = {1.0f, 0.0f};                      // Cumlative phase

            for (int i = 0; i < NP2; ++i)
            {
                w      *= wstep; // Update cumulative phase
                cd0[i] *= w;     // Apply phase shift
            }

            // Adjust the frequency and time offset.

            xdt = xdt2;
            f1 += delfbest;

            float const sync = syncjs8d(i0, 0.0f);

            std::array<std::array<float, NN>, NROWS> s2;

            for (int k = 0; k < NN; ++k)
            {
                // Calculate the starting index for the current symbol.

                int const i1 = ibest + k * Mode::NDOWNSPS;

                csymb.fill(ZERO);

                if (i1 >= 0 && i1 + Mode::NDOWNSPS <= NP2)
                {
                    std::copy(cd0.begin() + i1,
                              cd0.begin() + i1 + Mode::NDOWNSPS,
                              csymb.begin());
                }

                fftwf_execute(plans[Plan::CS]);

                // Normalize and take the magnitude of the first 8 points.

                for (int i = 0; i < NROWS; ++i)
                {
                    s2[i][k] = std::abs(csymb[i]) / 1000.0f;
                }
            }

            // Sync quality check using Costas tone patterns.

            int nsync = 0;

            for (std::size_t costas = 0; costas < Costas.size(); ++costas)
            {
                auto const offset = costas * 36;

                for (std::size_t column = 0; column < 7; ++column)
                {
                    // Find the row containing the maximum value in the
                    // current column.

                    auto const max_row = std::distance(
                        s2.begin(),
                        std::max_element(s2.begin(),
                                         s2.end(),
                                         [index = offset + column]
                                         (auto const & rowA,
                                          auto const & rowB)
                                         {
                                            return rowA[index] < rowB[index];
                                         }));

                    // Check if the max row matches the Costas pattern.

                    if (Costas[costas][column] == max_row) ++nsync;
                }
            }

            // If the sync quality isn't at least 7, this one's a loser.

            if (nsync <= 6) return std::nullopt;

            if (syncStats) emitEvent(JS8::Event::SyncState{JS8::Event::SyncState::Type::CANDIDATE,
                                                           Mode::NSUBMODE,
                                                           f1,
                                                           xdt,
                                                           {.candidate = nsync}});

            std::array<std::array<float, ND>, NROWS> s1;

            // Fill s1 from s2, excluding the Costas arrays.

            for (int row = 0; row < NROWS; ++row)
            {
                std::copy(s2[row].begin() +  7, s2[row].begin() + 36, s1[row].begin());
                std::copy(s2[row].begin() + 43, s2[row].begin() + 72, s1[row].begin() + 29);
            }

            // Temporary variables for metrics

            std::array<float, 3 * ND> llr0 = {};
            std::array<float, 3 * ND> llr1 = {};

            // Compute metrics for each row in `s1`

            for (int j = 0; j < ND; ++j)
            {
                int const i1 = 3 * j;     // First column (matches Fortran's i1)
                int const i2 = 3 * j + 1; // Second column (matches Fortran's i2)
                int const i4 = 3 * j + 2; // Third column (matches Fortran's i4)

                std::array<float, NROWS> ps;

                for (int i = 0; i < NROWS; ++i) ps[i] = s1[i][j];

                // Assign to `bmeta` in column order, with correct values
                llr0[i1] = std::max({ps[4], ps[5], ps[6], ps[7]}) - std::max({ps[0], ps[1], ps[2], ps[3]}); // r4
                llr0[i2] = std::max({ps[2], ps[3], ps[6], ps[7]}) - std::max({ps[0], ps[1], ps[4], ps[5]}); // r2
                llr0[i4] = std::max({ps[1], ps[3], ps[5], ps[7]}) - std::max({ps[0], ps[2], ps[4], ps[6]}); // r1

                for (auto & x : ps) x = std::log(x + 1e-32f);

                // Assign to `bmetb` in column order, with correct values
                llr1[i1] = std::max({ps[4], ps[5], ps[6], ps[7]}) - std::max({ps[0], ps[1], ps[2], ps[3]}); // r4
                llr1[i2] = std::max({ps[2], ps[3], ps[6], ps[7]}) - std::max({ps[0], ps[1], ps[4], ps[5]}); // r2
                llr1[i4] = std::max({ps[1], ps[3], ps[5], ps[7]}) - std::max({ps[0], ps[2], ps[4], ps[6]}); // r1
            }

            auto const normalizeLLR = [](auto & llr)
            {
                float sum            = 0.0f;
                float sum_of_squares = 0.0f;

                for (auto const value : llr)
                {
                    sum            += value;
                    sum_of_squares += value * value;
                }

                float const llrav    = sum            / llr.size();
                float const llr2av   = sum_of_squares / llr.size();
                float const variance = llr2av - llrav * llrav;
                float const llrsig   = std::sqrt(variance > 0.0f ? variance : llr2av);

                for (float & val : llr) val = (val / llrsig) * 2.83f;
            };

            // Normalize and process metrics

            normalizeLLR(llr0);
            normalizeLLR(llr1);

            std::array<int8_t, K> decoded;
            std::array<int8_t, N> cw;

            // Loop over decoding passes
            for (int ipass = 1; ipass <= 4; ++ipass)
            {
                // LLR 0 used on passes 1, 3, and 4; LLR 1 used on pass 2.

                auto const & llr = ipass == 2 ? llr1 : llr0;

                // Zero the first 24 bytes of LLR 0 on the third pass;
                // the first 48 bytes of LLR 0 on the fourth pass;

                if      (ipass == 3) std::fill(llr0.begin(),      llr0.begin() + 24, 0.0f);
                else if (ipass == 4) std::fill(llr0.begin() + 24, llr0.begin() + 48, 0.0f);

                // Decode using belief propagation.

                nharderrors = bpdecode174(llr, decoded, cw);
                xsnr        = -99.0f;

                // Check for all-zero codeword
                if (std::all_of(cw.begin(), cw.end(), [](int x) { return x == 0; }))
                {
                    continue;
                }

                if (nharderrors >= 0    && nharderrors < 60  &&
                    !(sync      <  2.0f && nharderrors > 35) &&
                    !(ipass     >  2    && nharderrors > 39) &&
                    !(ipass     == 4    && nharderrors > 30))
                {
                   if (checkCRC12(decoded))
                   {
                        if (syncStats) emitEvent(JS8::Event::SyncState{JS8::Event::SyncState::Type::DECODED,
                                                                       Mode::NSUBMODE,
                                                                       f1,
                                                                       xdt2,
                                                                       {.decoded = sync}});

                        auto message = extractmessage174(decoded);

                        int const i3bit = (decoded[72] << 2) |
                                          (decoded[73] << 1) |
                                           decoded[74];

                        std::array<int, NN> itone;

                        JS8::encode(i3bit, Costas, message.data(), itone.data());

                        // Subtract signal if needed.

                        if (lsubtract) subtractjs8(genjs8refsig(itone, f1), xdt2);

                        // Compute the signal power.

                        float xsig = 0.0f;

                        for (std::size_t i = 0; i < itone.size(); ++i)
                        {
                            xsig += std::pow(s2[itone[i]][i], 2);
                        }

                        // Compute SNR, clamping results lower than -28 to -28.
                        // Note that std::log10(1.259e-10) is about -9.9; we're
                        // avoiding undefined behavior in the log10 computation.

                        xsnr = std::max(
                            10.0f * std::log10(std::max(
                                xsig / xbase -  1.0f,
                                1.259e-10f)) - 32.0f,
                           -60.0f);  // XXX was -28.0f in Fortran

                        return std::make_optional<Decode>(i3bit, message);
                   }
                }
                else
                {
                    nharderrors = -1;
                }
            }

            return std::nullopt;
        }

        // Compute noise baseline. We differ quite a bit from the Fortran
        // implementation here.
        //
        // The Fortran version took `savg` as input; power scaled data from
        // `syncjs8`, and produced `sbase`, the noise baseline. To accomplish
        // that, it used up to 1000 lower envelope points for the polynomial
        // determination, which caused some oddities when the matrix was
        // ill-conditioned; we're just looking for a low-order polynomial
        // here, and a massively tall matrix isn't in general going to be
        // helpful there. Additionally, the methodology seemed to be very
        // susceptible to Runge's phenomenon.
        //
        // This approach instead uses a number of Chebyshev nodes proportional
        // to the polynomial degree, and we evaluate 2 kHz of `savg`, centered
        // around 1.5 kHz, to determine the polynomial, figuring that that's
        // going to be an optimal place to measure the 10% noise floor. We then
        // map the [ia, ib] range to the domain of the polynomial to compute
        // the baseline. Since `savg` would otherwise no longer be referenced
        // beyond this function, we dispense with `sbase` and instead overwrite
        // `savg` with the baseline.

        void
        baselinejs8(int const ia,
                    int const ib)
        {
            // Data referenced in savg is defined by the closed range [bmin, bmax].
            // From this we can derive the size of the closed range and the number
            // of points in each of the arms on either side of a node. All of these
            // values can be computed at compile time.

            using boost::math::ccmath::round;

            constexpr auto bmin = static_cast<std::size_t>(round(BASELINE_MIN / Mode::DF));
            constexpr auto bmax = static_cast<std::size_t>(round(BASELINE_MAX / Mode::DF));
            constexpr auto size = bmax - bmin + 1;
            constexpr auto arm  = size / (2 * BASELINE_NODES.size());

            // Loop invariants; beginning of the data range, sentinel one past the
            // end of the range.

            auto const data = savg.begin() + bmin;
            auto const end  = data + size;

            // Convert savg range of interest from power scale to dB scale.

            std::transform(data,
                           end,
                           data,
                           [](float const value)
                           {
                             return 10.0f * std::log10(value);
                           });

            // Collect lower envelope points; use Chebyshev node interpolants
            // to reduce Runge's phenomenon oscillations.

            for (std::size_t i = 0; i < BASELINE_NODES.size(); ++i)
            {
                auto const node = size * BASELINE_NODES[i];
                auto const base = data + static_cast<int>(std::round(node));
                auto       span = std::vector<float>(std::clamp(base - arm, data, end),
                                                     std::clamp(base + arm, data, end));

                auto const n = span.size() * BASELINE_SAMPLE / 100;

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

            // Solve the least squares problem for polynomial coefficients.

            c = V.colPivHouseholderQr().solve(y);

            // To map an index i in the range [ia, ib] to the polynomial's
            // input domain [0, size - 1]:
            //
            //      i  - ia
            //  x = ------- * (size - 1)
            //      ib - ia

            auto const mapIndex = [ia, ib, last = size - 1](int const i)
            {
                return (i - ia) * last / float(ib - ia);
            };

            // Replace savg with a computed baseline in the range [ia, ib].
            // This might be interpolation, which should be quite accurate,
            // or extrapolation, likely somewhat less so the further we get
            // from the polynomial fitting domain, but hopefully still good
            // enough for our purposes here.

            savg.fill(0.0f);

            for (int i = ia; i <= ib; ++i)
            {
                savg[i] = evaluate(mapIndex(i)) + 0.65f;
            }
        }

        // Extracted from the downsampling process; this step is part of the
        // frequency-domain filtering process for downsampling the JS8 signal.
        // After the FFT, the resulting frequency-domain data (ds_cx) can be
        // manipulated (e.g., band-pass filtered or shifted). Subsequent inverse
        // FFT operations convert the filtered data back to the time domain at
        // a lower sample rate, achieving the desired downsampling.

        void
        computeBasebandFFT()
        {
            // ds_dx is an array of complex<float>; we're going to do an in-place
            // FFT, so we'll interpret the first half of the array as if they were
            // floats, which they are.

            float * fftw_real = reinterpret_cast<float *>(ds_cx.data());

            // Copy in data and zero-pad any remainder; not all modes will have
            // a remainder.

            std::copy(dd.begin(), dd.end(),  fftw_real);
            std::fill(fftw_real + dd.size(), fftw_real + Mode::NDFFT1, 0.0f);

            fftwf_execute(plans[Plan::BB]);
        }

        // This function extracts a narrow frequency band around the target frequency f0,
        // applies tapering to reduce spectral artifacts, aligns the signal to the center
        // frequency, performs an inverse FFT to convert the data back into the time domain,
        // and normalizes the result for further processing in the JS8 decoding pipeline.

        void
        js8_downsample(float const f0)
        {
            // Frequency band extraction; identifies a narrow frequency band around the
            // target frequency (f0) based on a predefined range (8.5 baud above and 1.5
            // baud below). The indices of this range in the frequency-domain representation
            // (ds_cx) are calculated (ib and it), and the relevant frequency-domain samples
            // are extracted into cd0.

            constexpr float DF   = 12000.0f / Mode::NDFFT1;
            constexpr float BAUD = 12000.0f / Mode::NSPS;

            float const ft = f0 + 8.5f * BAUD;
            float const fb = f0 - 1.5f * BAUD;
            int   const i0 =             static_cast<int>(std::round(f0 / DF));
            int   const it = std::min(   static_cast<int>(std::round(ft / DF)), Mode::NDFFT1 / 2);
            int   const ib = std::max(0, static_cast<int>(std::round(fb / DF)));

            std::size_t const NDD_SIZE = Mode::NDD + 1;
            std::size_t const RANGE_SIZE = it - ib + 1;

            std::fill_n(cd0.begin(), Mode::NDFFT2, ZERO);

            std::copy(ds_cx.begin() + ib,
                      ds_cx.begin() + ib + RANGE_SIZE,
                      cd0.begin());

            // Tapering is applied to smooth the edges of the frequency band, reducing
            // spectral leakage during the inverse FFT. Reversed taper at the beginning,
            // normal taper at the end.

            auto const head = cd0.begin();
            auto const tail = cd0.begin() + RANGE_SIZE;

            std::transform(head, head + NDD_SIZE, Taper[0].begin(), head,            std::multiplies<>());
            std::transform(tail - NDD_SIZE, tail, Taper[1].begin(), tail - NDD_SIZE, std::multiplies<>());

            // The extracted frequency band is aligned to the center of the frequency domain
            // representation (i0 - ib) via a cyclic shift using std::rotate. This centers
            // the desired signal.

            std::rotate(cd0.begin(), cd0.begin() + (i0 - ib), cd0.begin() + Mode::NDFFT2);

            // An inverse FFT is performed on the frequency-domain data (cd0) to transform it
            // back into the time domain, effectively yielding a downsampled, time-domain signal
            // focused on the extracted narrow frequency band.

            fftwf_execute(plans[Plan::DS]);

            // The resulting time-domain samples are normalized by a factor derived from the
            // input and output FFT sizes (Mode::NDFFT1 and Mode::NDFFT2), ensuring consistency
            // in the signal’s amplitude.

            float const factor = 1.0f / std::sqrt(static_cast<float>(Mode::NDFFT1) * Mode::NDFFT2);

            std::transform(cd0.begin(),
                           cd0.end(),
                           cd0.begin(),
                           [factor](auto & value) { return value * factor; });
        }

        // Evaluate the synchronization power of signal segments, ranks potential candidates, and
        // extracts the most promising ones for further decoding.
        //
        // Detailed Steps:
        //
	    // 1.  Compute Symbol Spectra:
        //
	    //     - The signal is processed in overlapping segments, with each segment multiplied by
        //       a Nuttall window to reduce spectral leakage.
	    //     - An FFT is performed on each windowed segment to obtain the frequency-domain
        //       representation.
	    //     - The power spectrum of each segment is computed, and the average spectrum is
        //       accumulated across segments.
        //
	    // 2.  Filter Edge Adjustments:
	    //
        //     - Adjusts the frequency bounds (nfa and nfb) to ensure the analysis remains
        //       within valid and meaningful regions of the signal.
        //
	    // 3.  Baseline Computation:
	    //
        //     - The average spectrum is converted to a dB scale.
	    //     - Baseline is computed to distinguish significant signal components from
        //       background noise.
        //
	    // 4.  Synchronization Metric Calculation:
	    //
        //     - For each frequency bin in the specified range, evaluates synchronization
        //       power using a Costas waveform.
	    //     - Sync metric is computed over the index range, considering all combinations
        //       of Costas patterns.
	    //     - The maximum sync value and its corresponding offset are recorded for each
        //       frequency bin.
	    //
        // 5.  Normalization:
	    //
        //     - The sync values are normalized to the 40th percentile value using a ranked
        //       index. This ensures a consistent scaling across different signals and noise
        //       levels.
        //
	    // 6.  Candidate Extraction:
        //
        //     - Candidates with a strong sync metric (above a defined threshold) are extracted.
	    //     - Near-duplicate candidates of lesser synchronization power, based on frequency
        //       proximity, are eliminated.
        //
	    // 7.  Output:
        //
        //	   - Returns a vector of the most promising signal candidates, sorted by their
        //       synchronization power. It's expected that these will be re-sorted by the
        //       caller into a desirable order, but synchronization power order facilitates
        //       debugging this function.
        //
        // Note: The Fortran version of this routine would normalize `s` at the end of this
        //       function, but I'm unsure why; nothing beyond this function references `s`,
        //       so it was effectively a somewhat expensive dead store. It's been eliminated
        //       in this version.

        std::vector<Sync>
        syncjs8(int nfa,
                int nfb)
        {
            // Compute symbol spectra

            savg.fill(0.0f);

            for (int j = 0; j < Mode::NHSYM; ++j)
            {
                int const ia = j  * Mode::NSTEP;
                int const ib = ia + Mode::NFFT1;

                if (ib > Mode::NMAX) break;

                std::transform(dd.begin() + ia,
                               dd.begin() + ib,
                               nuttal.begin(),
                               reinterpret_cast<float *>(sd.data()),
                               std::multiplies<float>{});

                fftwf_execute(plans[Plan::SD]);

                // Compute power spectrum

                for (int i = 0; i < Mode::NSPS; ++i)
                {
                    auto const power = std::norm(sd[i]);
                    s[i][j]  = power;
                    savg[i] += power;
                }
            }

            // Filter edge sanity measures

            int const nwin = nfb - nfa;

            if (nfa < 100)
            {
                nfa = 100;
                if (nwin < 100) nfb = nfa + nwin;
            }

            if (nfb > 4910)
            {
                nfb = 4910;
                if (nwin < 100) nfa = nfb - nwin;
            }

            auto const ia = std::max(0, static_cast<int>(std::round(nfa / Mode::DF)));
            auto const ib =             static_cast<int>(std::round(nfb / Mode::DF));

            // Convert average spectrum from power to db scale and compute
            // baseline from it; baseline replaces average spectrum.

            baselinejs8(ia, ib);

            // Compute and populate the sync index.

            sync.clear();

            for (int i = ia; i <= ib; ++i)
            {
                float max_value = -std::numeric_limits<float>::infinity();
                int   max_index = -Mode::JZ;

                for (int j = -Mode::JZ; j <= Mode::JZ; ++j)
                {
                    std::array<std::array<float, 3>, 2> t{};

                    for (int p = 0; p < 3; ++p)
                    {
                        for (int n = 0; n < 7; ++n)
                        {
                            int const offset = j + Mode::JSTRT + NSSY * n + p * 36 * NSSY;

                            if (offset >= 0 && offset < Mode::NHSYM)
                            {
                                // Accumulate Costas pattern contributions.

                                t[0][p] += s[i + NFOS * Costas[p][n]][offset];

                                // Accumulate sum over all frequencies for this block.

                                for (int freq = 0; freq < 7; ++freq)
                                {
                                    t[1][p] += s[i + NFOS * freq][offset];
                                }
                            }
                        }
                    }

                    // Compute sync metric over the index range. We are at the moment
                    // maintaining the Fortran summation methodology for compatibility
                    // testing; there are more efficient ways to do this, but IEEE 754
                    // addition is a touchy thing, so we'll need to ensure that any
                    // changes don't negatively affect result precision.

                    auto const compute_sync = [&t](int start, int end)
                    {
                        float tx = 0.0f;
                        float t0 = 0.0f;

                        for (int i = start; i <= end; ++i)
                        {
                            tx += t[0][i];
                            t0 += t[1][i];
                        }

                        return tx / ((t0 - tx) / 6.0f);
                    };

                    if (auto const sync_value = std::max({
                            compute_sync(0, 2),
                            compute_sync(0, 1),
                            compute_sync(1, 2)
                        }); sync_value > max_value)
                    {
                        max_value = sync_value;
                        max_index = j;
                    }
                }

                sync.emplace(Mode::DF    * i,
                             Mode::TSTEP * (max_index + 0.5f),
                                            max_value);
            }

            // If we found nothing, we're done here.

            if (sync.empty()) return {};

            // Access the sync indices.

            auto & freqIndex = sync.get<Tag::Freq>();
            auto & rankIndex = sync.get<Tag::Rank>();
            auto & syncIndex = sync.get<Tag::Sync>();

            // Normalize to the 40th percentile using the frequency index,
            // which is stable under sync value mutation. One thing to note
            // here is that the Fortran version didn't seem to reliably
            // calculate the 40th percentile rank; sometimes high, other
            // times low, infrequently actually the 40th percentile value.
            // This method should be perfectly accurate in all cases.

            auto const normalize =
            [
               sync = rankIndex.nth(rankIndex.size() * 4 / 10)->sync
            ]
            (Sync & entry)
            {
                entry.sync /= sync;
            };

            for (auto it  = freqIndex.begin();
                      it != freqIndex.end();
                    ++it)
            {
                freqIndex.modify(it, normalize);
            }

            // Extract candidates.

            std::vector<Sync> candidates;

            for (auto it  = syncIndex.begin();
                      it != syncIndex.end() && candidates.size() < NMAXCAND;
                      it  = syncIndex.begin())
            {
                // Stop iteration if below threshold or invalid; as the
                // index is sorted by sync, any subsequent entries will
                // also be below the threshold or invalid.

                if (it->sync < ASYNCMIN || std::isnan(it->sync)) break;

                // Good value, relatively strong; save the candidate.

                candidates.push_back(*it);

                // Remove the candidate and any near-duplicates based
                // on frequency. This invalidates `it`, so we reset it
                // to the index begin in the loop increment condition.

                freqIndex.erase(
                    freqIndex.lower_bound(it->freq - Mode::AZ),
                    freqIndex.upper_bound(it->freq + Mode::AZ));
            }

            return candidates;
        }

        // Returns the total synchronization power, which is a measure of how well
        // the signal aligns with the Costas sequence after accounting for the
        // frequency adjustment. Used to identify the best alignment for further
        // decoding.

        float
        syncjs8d(int   const i0,
                 float const delf)
        {
            constexpr float BASE_DPHI = TAU * (1.0f / (12000.0f / Mode::NDOWN));

            // If delta frequency is non-zero, compute the frequency
            // adjustment array, otherwise, use what'll be an identity
            // transfrom when multiplied.

            std::array<std::complex<float>, Mode::NDOWNSPS> freqAdjust;

            if (delf != 0.0f)
            {
                float const dphi = BASE_DPHI * delf;
                float       phi  = 0.0f;

                // std::fmod() is almost like Fortran's mod(), but not quite;
                // Since delf can be negative, we must ensure that phi stays
                // within [0, TAU), which Fortran's mod() handles by itself.

                for (int i = 0; i < Mode::NDOWNSPS; ++i)
                {
                    freqAdjust[i] = std::polar(1.0f, phi);
                    if (phi = std::fmod(phi + dphi, TAU);
                        phi < 0.0f)
                    {
                        phi += TAU;
                    }
                }
            }
            else
            {
                freqAdjust.fill(std::complex<float>{1.0f, 0.0f});
            }

            // Compute sync power by looping over the Costas indices for
            // each of the 3 Costas blocks, accumulating as we go.

            float sync = 0.0f;

            for (int i = 0; i < 3; ++i)
            {
                for (int j = 0; j < 7; ++j)
                {
                    if (auto const offset = 36 * i * Mode::NDOWNSPS
                                          + i0 + j * Mode::NDOWNSPS; offset >= 0 &&
                                            offset + Mode::NDOWNSPS <= Mode::NP2)
                    {
                        sync += std::norm(
                            std::transform_reduce(
                                freqAdjust.begin(),     // Range start
                                freqAdjust.end(),       // Range end
                                cd0.begin() + offset,   // Data start
                                std::complex<float>{},  // Initial reduction value
                                std::plus<>{},          // Reduction by accumulation
                                [&](auto const & fa,    // Conjugate and multiply
                                    auto const & cd)
                                {
                                    return cd * std::conj(fa * csyncs[i][j][&fa - &freqAdjust[0]]);
                                }
                            ));
                    }
                }
            }

            return sync;
        }

        // Generate a reference signal, based on the provided tone sequence and
        // base frequency. The output is a vector of complex values representing
        // the signal in the time domain.

        std::vector<std::complex<float>>
        genjs8refsig(std::array<int, NN> const & itone,
                     float               const   f0)
        {
            // Precompute the base frequency contribution; full circle in
            // radians, multipled by the base frequency, multiplied by the
            // sampling interval, i.e., the time step between samples, which
            // results in the base frequency phase increment. Start the
            // phase accumulator off at zero.

            float const BFPI = TAU * f0 * (1.0f / 12000.0f);
            auto        phi  = 0.0f;

            std::vector<std::complex<float>> cref;
            cref.reserve(NN * Mode::NSPS);

            for (int i = 0; i < NN; ++i)
            {
                // Compute phase increment for the tone; frequency offset is
                // determined by the tone value.

                float const dphi = BFPI + TAU * static_cast<float>(itone[i]) / Mode::NSPS;

                // Iterate over the samples per symbol to generate the time
                // domain signal.

                for (std::size_t is = 0; is < Mode::NSPS; ++is)
                {
                    cref.push_back(std::polar(1.0f, phi));
                    phi = std::fmod(phi + dphi, TAU);
                }
            }

            return cref;
        }

        // Subtract a JS8 signal
        //
        // Measured signal  : dd(t)    = a(t)cos(2*pi*f0*t+theta(t))
        // Reference signal : cref(t)  = exp( j*(2*pi*f0*t+phi(t)) )
        // Complex amp      : cfilt(t) = LPF[ dd(t)*CONJG(cref(t)) ]
        // Subtract         : dd(t)    = dd(t) - 2*REAL{cref*cfilt}
        //
        // Important to note that dt can be negative here.

        void
        subtractjs8(std::vector<std::complex<float>> const & cref,
                    float                            const   dt)
        {
            auto        const nstart     = static_cast<int>(dt * 12000.0f);
            std::size_t const cref_start = (nstart < 0) ? static_cast<std::size_t>(-nstart) : 0;
            std::size_t const dd_start   = (nstart > 0) ? static_cast<std::size_t>( nstart) : 0;
            auto        const size       = std::min(cref.size() - cref_start, dd.size() - dd_start);

            // Populate complex filter with the conjugate of the reference signal.

            for (std::size_t i = 0; i < size; ++i)
            {
                cfilt[i] = dd[dd_start + i] * std::conj(cref[cref_start + i]);
            }

            // Zero-fill the remainder, if any.

            std::fill(cfilt.begin() + size, cfilt.end(), ZERO);

            // FFT to the frequency domain.

            fftwf_execute(plans[Plan::CF]);

            // Apply the filter in the frequency domain.

            std::transform(cfilt.begin(),
                           cfilt.end(),
                           filter.begin(),
                           cfilt.begin(),
                           std::multiplies<>());

            // Inverse FFT to return to the time domain.

            fftwf_execute(plans[Plan::CB]);

            // Subtract the reconstructed signal.

            for (std::size_t i = 0; i < size; ++i)
            {
                dd[dd_start + i] -= 2.0f * std::real(cfilt[i] * cref[cref_start + i]);
            }
        }

    public:

        // Constructor

        DecodeMode()
        {
            // Intialize the Nuttal window. In theory, we can do this as a
            // constexpr function at compile time, but doing so yield results
            // slightly different than the Fortran version did, so for sanity
            // while testing, we'll opt for consistency. IEEE 754 is always a
            // bit brittle.

            constexpr float a0 =  0.3635819f;
            constexpr float a1 = -0.4891775f;
            constexpr float a2 =  0.1365995f;
            constexpr float a3 = -0.0106411f;

            // Computed Pi constant to match the Fortran version; we could
            // probably use M_PI here, but for the moment, matching Fortran
            // exactly.

            float const pi  = 4.0f * std::atan(1.0f);
            float       sum = 0.0f;

            for (std::size_t i = 0; i < nuttal.size(); ++i)
            {
                // Naive summation here will exhibit substantial precision loss
                // relative to the Fortran version; we use Kahan summation to
                // compensate, which should yield results identical to Fortran.

                KahanSum value = a0;

                value += a1 * std::cos(2 * pi * i / nuttal.size());
                value += a2 * std::cos(4 * pi * i / nuttal.size());
                value += a3 * std::cos(6 * pi * i / nuttal.size());

                nuttal[i] = value;
                sum      += value;
            }

            // Normalize the Nuttal window.

            for (auto & value : nuttal) value = value / sum * nuttal.size() / 300.0f;

            // Initialize Costas waveforms.

            for (int i = 0; i < 7; ++i)
            {
                float const dphia = TAU * Costas[0][i] / Mode::NDOWNSPS;
                float const dphib = TAU * Costas[1][i] / Mode::NDOWNSPS;
                float const dphic = TAU * Costas[2][i] / Mode::NDOWNSPS;

                float phia = 0.0f;
                float phib = 0.0f;
                float phic = 0.0f;

                for (int j = 0; j < Mode::NDOWNSPS; ++j)
                {
                    csyncs[0][i][j] = std::polar(1.0f, phia);
                    csyncs[1][i][j] = std::polar(1.0f, phib);
                    csyncs[2][i][j] = std::polar(1.0f, phic);

                    phia = std::fmod(phia + dphia, TAU);
                    phib = std::fmod(phib + dphib, TAU);
                    phic = std::fmod(phic + dphic, TAU);
                }
            }

            // Compute a Hann-like window directly into the real part of the
            // first NFILT + 1 elements in the filter, accumulating the sum
            // as we go.

            sum = 0.0f;

            for (int j = -NFILT / 2; j <= NFILT / 2; ++j)
            {
                int   const index = j + NFILT / 2;
                float const value = std::pow(std::cos(pi * j / NFILT), 2);

                filter[index].real(value);
                sum             += value;
            }

            // Now that we've got the sum, create actual complex numbers using
            // the normalized real values that we just populated and zero the
            // rest of the filter.

            std::fill(std::transform(filter.begin(),
                                     filter.begin() + NFILT + 1,
                                     filter.begin(),
                                     [sum](auto const value)
                                     {
                                         return std::complex<float>(value.real() / sum, 0.0f);
                                     }),
                      filter.end(),
                      ZERO);

            // Shift to position the window.

            std::rotate(filter.begin(),
                        filter.begin() + NFILT / 2,
                        filter.begin() + NFILT + 1);

            // Transform the filter into the frequency domain.

            fftwf_plan fftw_plan;
            {
                std::lock_guard<std::mutex> lock(fftw_mutex);

                fftw_plan = fftwf_plan_dft_1d(Mode::NMAX,
                                              reinterpret_cast<fftwf_complex *>(filter.data()),
                                              reinterpret_cast<fftwf_complex *>(filter.data()),
                                              FFTW_FORWARD,
                                              FFTW_ESTIMATE_PATIENT);

                if (!fftw_plan)
                {
                    throw std::runtime_error("Failed to create FFT plan");
                }
            }

            fftwf_execute(fftw_plan);

            {
                std::lock_guard<std::mutex> lock(fftw_mutex);
                fftwf_destroy_plan(fftw_plan);
            }

            // Normalize the frequency domain representation.

            std::transform(filter.begin(),
                           filter.end(),
                           filter.begin(),
                           [factor = 1.0f / Mode::NMAX](auto value)
                           {
                               return value * factor;
                           });

            // The rest of our FFT plans are always the same size and operate on the
            // same data, so we can reuse them as long as we're alive.

            std::lock_guard<std::mutex> lock(fftw_mutex);

            plans[Plan::DS] = fftwf_plan_dft_1d(Mode::NDFFT2,
                                                reinterpret_cast<fftwf_complex *>(cd0.data()),
                                                reinterpret_cast<fftwf_complex *>(cd0.data()),
                                                FFTW_BACKWARD,
                                                FFTW_ESTIMATE_PATIENT);

            plans[Plan::BB] = fftwf_plan_dft_r2c_1d(Mode::NDFFT1,
                                                    reinterpret_cast<float         *>(ds_cx.data()),
                                                    reinterpret_cast<fftwf_complex *>(ds_cx.data()),
                                                    FFTW_ESTIMATE_PATIENT);

            plans[Plan::CF] = fftwf_plan_dft_1d(Mode::NMAX,
                                                reinterpret_cast<fftwf_complex *>(cfilt.data()),
                                                reinterpret_cast<fftwf_complex *>(cfilt.data()),
                                                FFTW_FORWARD,
                                                FFTW_ESTIMATE_PATIENT);

            plans[Plan::CB] = fftwf_plan_dft_1d(Mode::NMAX,
                                                reinterpret_cast<fftwf_complex *>(cfilt.data()),
                                                reinterpret_cast<fftwf_complex *>(cfilt.data()),
                                                FFTW_BACKWARD,
                                                FFTW_ESTIMATE_PATIENT);

            plans[Plan::SD] = fftwf_plan_dft_r2c_1d(Mode::NFFT1,
                                                    reinterpret_cast<float         *>(sd.data()),
                                                    reinterpret_cast<fftwf_complex *>(sd.data()),
                                                    FFTW_ESTIMATE_PATIENT);

            plans[Plan::CS] = fftwf_plan_dft_1d(Mode::NDOWNSPS,
                                                reinterpret_cast<fftwf_complex *>(csymb.data()),
                                                reinterpret_cast<fftwf_complex *>(csymb.data()),
                                                FFTW_FORWARD,
                                                FFTW_ESTIMATE_PATIENT);

            for (auto plan : plans)
            {
                if (!plan) throw std::runtime_error("Failed to create FFT plan");
            }
        }

        // Decode entry point.

        std::size_t
        operator()(struct dec_data const & data,
                   int             const   kpos,
                   int             const   ksz,
                   JS8::Event::Emitter     emitEvent)
        {
            // Copy the relevant frames for decoding

            auto const pos = std::max(0, kpos);
            auto const sz  = std::max(0, ksz);

            assert(sz <= Mode::NMAX);

            if (data.params.syncStats) emitEvent(JS8::Event::SyncStart{pos, sz});

            auto const ddCopy = [](auto const begin,
                                   auto const end,
                                   auto const to)
            {
                std::transform(begin, end, to, [](auto const value)
                {
                    return static_cast<float>(value);
                });
            };

            dd.fill(0.0f);

            if ((JS8_RX_SAMPLE_SIZE - pos) < sz)
            {
                // Wrap case; split into two parts.

                int const firstsize  = JS8_RX_SAMPLE_SIZE - pos;
                int const secondsize = sz - firstsize;

                ddCopy(std::begin(data.d2) + pos, std::begin(data.d2) + pos + firstsize,  dd.begin());
                ddCopy(std::begin(data.d2),       std::begin(data.d2) +       secondsize, dd.begin() + firstsize);
            }
            else
            {
                // Non-wrapping case; copy directly.

                ddCopy(std::begin(data.d2) + pos, std::begin(data.d2) + pos + sz, dd.begin());
            }

            Decode::Map decodes;

            for (int ipass = 1; ipass <= 3; ++ipass)
            {
                // Determine if there's anything worth considering in the signal.
                // If not, then we can just bail completely; more passes will not
                // yield more results. If we do have some candidates, sort them
                // by frequency, but put any that are close to nfqso up front.

                auto candidates = syncjs8(data.params.nfa,
                                          data.params.nfb);

                if (candidates.empty()) break;

                std::sort(candidates.begin(),
                          candidates.end(),
                          [nfqso = data.params.nfqso](auto const & a,
                                                      auto const & b)
                          {
                            auto const a_dist = std::abs(a.freq - nfqso);
                            auto const b_dist = std::abs(b.freq - nfqso);

                            if (a_dist < 10.0f && b_dist >= 10.0f) return true;
                            if (b_dist < 10.0f && a_dist >= 10.0f) return false;

                            return std::tie(a_dist, a.freq) <
                                   std::tie(b_dist, b.freq);
                          });

                // Recompute the baseband signal; subtraction during the last
                // pass might have changed the landscape.

                computeBasebandFFT();

                bool const subtract = ipass < 3;
                bool       improved = false;

                for (auto [f1, xdt, sync] : candidates)
                {
                    float xsnr        =  0.0f;
                    int   nharderrors = -1;

                    if (auto decode = js8dec(data.params.syncStats,
                                             subtract,
                                             f1,
                                             xdt,
                                             nharderrors,
                                             xsnr,
                                             emitEvent))
                    {
                        // We don't need to be emitting duplicate events for something
                        // that's effectively the same SNR as a previous event.

                        auto const snr = static_cast<int>(std::round(xsnr));

                        // If this decode is new, or it's a duplicate with a better SNR
                        // than what we had before, then our situation has improved and
                        // we must announce that we've had some success.

                        if (auto [it, inserted] = decodes.try_emplace(std::move(*decode), snr);
                                      inserted || it->second < snr)
                        {
                            improved = true;

                            // Update the SNR if this is an improved decode.

                            if (!inserted) it->second = snr;

                            // Emit decoded events on new or improved decodes.

                            emitEvent(JS8::Event::Decoded{data.params.nutc,
                                                          snr,
                                                          xdt - Mode::ASTART,
                                                          f1,
                                                          it->first.data,
                                                          it->first.type,
                                                          1.0f - nharderrors / 60.0f,
                                                          Mode::NSUBMODE});
                        }
                    }
                }

                // If nothing from this pass improved our situation, there's no
                // point in trying any remaining passes.

                if (!improved) break;
            }

            // Let the caller know how many unique decodes we discovered, if any.

            return decodes.size();
        }
    };

    // Explicit template class instantiations; avoids compiler complaints
    // about unused variables.

    template class DecodeMode<ModeA>;
    template class DecodeMode<ModeB>;
    template class DecodeMode<ModeC>;
    template class DecodeMode<ModeE>;
    template class DecodeMode<ModeI>;
}

/******************************************************************************/
// Worker
/******************************************************************************/

namespace JS8
{
    class Worker : public QObject
    {
        Q_OBJECT

        // Initialization of the decoders, in that they're heavy with
        // FFT plan creations, is non-trivial, so a handle-body class
        // to avoid initializing them on the main thread.

        class Impl
        {
            // To avoid data races, decode data is referenced here but is
            // actually located in the Worker that instantiates us, as it
            // must be possible to copy data for us before we're ready to
            // process it.

            struct dec_data & m_data;

            // Mode-specific decode strategy; we'll instantiate one of
            // these for each of the 5 modes; this class is an aggregate
            // of the 5 modes.

            struct DecodeEntry
            {
                std::variant<
                    DecodeMode<ModeA>,
                    DecodeMode<ModeB>,
                    DecodeMode<ModeC>,
                    DecodeMode<ModeE>,
                    DecodeMode<ModeI>
                >     decode;
                int   mode;
                int & kpos;
                int & ksz;

                template <typename DecodeModeType>
                DecodeEntry(std::in_place_type_t<DecodeModeType>,
                            int   mode,
                            int & kpos,
                            int & ksz)
                    : decode(std::in_place_type<DecodeModeType>)
                    , mode  (mode)
                    , kpos  (kpos)
                    , ksz   (ksz)
                    {}
            };

            // Since a strategy can be neither moved nor copied, we must
            // instantiate them in-place. Note that with the advent of the
            // multi-decoder, mode identifiers became a bitset instead of
            // integral values. The order defined here is the order that
            // the decode loop will run in; we're matching the Fortran
            // version here in terms of faster modes first.

            template <typename ModeType>
            DecodeEntry makeDecodeEntry(int   shift,
                                        int & kpos,
                                        int & ksz)
            {
                return DecodeEntry(std::in_place_type<DecodeMode<ModeType>>,
                                   1 << shift,
                                   kpos,
                                   ksz);
            }

            std::array<DecodeEntry, 5> m_decodes =
            {{
                makeDecodeEntry<ModeI>(4, m_data.params.kposI, m_data.params.kszI),
                makeDecodeEntry<ModeE>(3, m_data.params.kposE, m_data.params.kszE),
                makeDecodeEntry<ModeC>(2, m_data.params.kposC, m_data.params.kszC),
                makeDecodeEntry<ModeB>(1, m_data.params.kposB, m_data.params.kszB),
                makeDecodeEntry<ModeA>(0, m_data.params.kposA, m_data.params.kszA)
            }};

        public:

            // Constructor

            explicit Impl(struct dec_data & data)
            : m_data(data)
            {}

            // Execute a decoding pass, using the supplied event emitter to
            // emit events as they occur.

            void operator()(Event::Emitter emitEvent)
            {
                // The multi-decoder can provide data for multiple modes at
                // the same time; specific decodes to be performed for this
                // pass are in the `nsubmodes` bitset.

                auto const  set = m_data.params.nsubmodes;
                std::size_t sum = 0;

                // Let any interested parties know that we've started a run
                // for the set of modes requested.

                emitEvent(Event::DecodeStarted{set});

                // Iterate through all the modes we're aware of, performing
                // a mode-specific decode pass if the mode is scheduled for
                // decoding during this pass.

                for (auto & entry : m_decodes)
                {
                    if ((set & entry.mode) == entry.mode)
                    {
                        std::visit([&](auto && decode) {
                            sum += decode(m_data,
                                          entry.kpos,
                                          entry.ksz,
                                          emitEvent);
                        }, entry.decode);
                    }
                }

                // Let any interested parties know the total number of decodes
                // performed during this run.

                emitEvent(Event::DecodeFinished{sum});
            }
        };

        // Data members

        QSemaphore      * m_semaphore;
        std::atomic<bool> m_quit = false;
        struct dec_data   m_data;

    public:

        // Constructor

        explicit Worker(QSemaphore * semaphore,
                        QObject    * parent = nullptr)
        : QObject    (parent)
        , m_semaphore(semaphore)
        {}

        // Used to inform the worker that it's time to go; the next
        // time it wakes up due to the semaphore being released, it
        // will exit the runloop.

        void stop()
        {
            m_quit = true;
        }

        // Called by the owning Decoder to refresh the copy of the
        // decode data that the Worker implementation references.

        void copy()
        {
            m_data = dec_data;
        };

    signals:

        // Signal used to indicate that something of interest has
        // occurred during a decoding pass.

        void decodeEvent(Event::Variant const &);

    public slots:

        // Runloop for the thread that the worker is scheduled on; this
        // is started by the Decoder when it's informed that the thread
        // has started. Performs decoding runs each time the semaphore
        // is released, until it's informed that it should quit.

        void run()
        {
            // Our thread has started, and we're now running on it, so
            // we're good to now allocate our implementation; we didn't
            // want that to happen on the main thread, as the FFT plans
            // can take a while. We only need the implementation while
            // we're running.

            std::unique_ptr<Impl> impl = std::make_unique<Impl>(m_data);

            // Wait until there's something that requires our attention,
            // which is going to either be needing to quit or needing to
            // perform a decoding pass.

            while (true)
            {
                m_semaphore->acquire();

                if (m_quit) break;

                (*impl)([this](Event::Variant const & event)
                {
                    emit decodeEvent(event);
                });
            }
        }
    };
}

/******************************************************************************/
// Public Interface - Decoding
/******************************************************************************/

#include "JS8.moc"

namespace JS8
{
    Decoder::Decoder(QObject * parent)
    : QObject(parent)
    , m_semaphore(0)
    , m_worker(new Worker(&m_semaphore))
    {
        m_worker->moveToThread(&m_thread);

        connect(&m_thread, &QThread::started,    m_worker, &Worker::run);
        connect(&m_thread, &QThread::finished,   m_worker, &QObject::deleteLater);
        connect(m_worker,  &Worker::decodeEvent, this,     &Decoder::decodeEvent);
    }

    void
    Decoder::start(QThread::Priority priority)
    {
        m_thread.start(priority);
    }

    void
    Decoder::quit()
    {
        m_worker->stop();
        m_semaphore.release();
        m_thread.quit();
        m_thread.wait();
    }

    void
    Decoder::decode()
    {
        m_worker->copy();
        m_semaphore.release();
    }
}

/******************************************************************************/
// Public Interface - Encoding
/******************************************************************************/

namespace JS8
{
    // Port of the Fortran `genjs8` subroutine; from the 12 bytes of `message`,
    // construct an 87-bit JS8 message and encode it into tones. Costas array
    // to use supplied by the caller, as is the type of message, indicated by
    // the lower 3 bits of `type`.

    void
    encode(int           const   type,
           Costas::Array const & costas,
           const char  * const   message,
           int         * const   tones)
    {
        // Our initial goal here is an 87-bit message, for which a std::bitset
        // would be the obvious choice, but we've got to compute a checksum of
        // the first 75 bits; thus, an array instead.
        //
        // Message structure:
        //
        //     +----------+----------+----------+
        //     |          |          |  72 bits |  12 6-bit words
        //     |          |          +==========+
        //     |          | 87 bits  |   3 bits |  Frame type
        //     | 11 bytes |          +==========+
        //     |          |          |  12 bits |  12-bit BE checksum
        //     |          |----------+==========+
        //     |          |  1 bit   |   1 bit  |  Leftover bit in array
        //     +----------+----------+==========+

        std::array<std::uint8_t, 11> bytes = {};

        // Convert the 12 characters we've been handed to 6-bit words and pack
        // them into the byte array, 4 characters, 24 bits at a time, into the
        // 9 bytes [0,8], 72 bits total. Throws if handed an invalid character.

        for (int i = 0, j = 0; i < 12; i += 4, j += 3)
        {
            std::uint32_t words = (alphabetWord(message[i    ]) << 18) |
                                  (alphabetWord(message[i + 1]) << 12) |
                                  (alphabetWord(message[i + 2]) <<  6) |
                                   alphabetWord(message[i + 3]);

            bytes[j    ] = words >> 16;
            bytes[j + 1] = words >>  8;
            bytes[j + 2] = words;
        }

        // The bottom 3 bits of type are the frame type; these go into the
        // next 3 bits in the byte array, i.e., the first 3 bits of byte 9,
        // after which we'll be at 75 bits in total.

        bytes[9] = (type & 0b111) << 5;

        // We now need to compute the augmented CRC-12 of the complete
        // byte array, including the trailing zero bits that we've not
        // set yet.

        auto const crc = CRC12(bytes);

        // That CRC needs to occupy the next 12 bits of the array, i.e.,
        // the final 5 bits of byte 9, and the first 7 bits of byte 10.

        bytes[9] |= (crc >> 7) & 0x1F;
        bytes[10] = (crc & 0x7F) << 1;

        // That's it for our 87-bit message; we're now going to turn it
        // into two blocks of 29 3-bit words, which will in turn become
        // tones, the first block being parity for the second, bracketed
        // by the Costas arrays.
        //
        // Output structure:
        //
        //     +----------+----------+
        //     |          |  7 bytes |  Costas array A
        //     |          +==========+
        //     |          | 29 bytes |  Parity data
        //     |          +==========+
        //     | 79 bytes |  7 bytes |  Costas array B
        //     |          +==========+
        //     |          | 29 bytes |  Output data
        //     |          +==========+
        //     |          |  7 bytes |  Costas array C
        //     +----------+==========+

        auto costasData = tones;
        auto parityData = tones + 7;
        auto outputData = tones + 43;

        // Output the 3 Costas arrays at offsets 0, 36, and 72.

        for (auto const & array : costas)
        {
            std::copy(array.begin(), array.end(), costasData);
            costasData += 36;
        }

        // Our 87 bits are going to be morphed into two sets of 29 3-bit
        // words, the first one parity for the second; we're going to do
        // this in parallel.

        std::size_t  outputBits  = 0;
        std::size_t  outputByte  = 0;
        std::uint8_t outputMask  = 0x80;
        std::uint8_t outputWord  = 0;
        std::uint8_t parityWord  = 0;

        for (std::size_t i = 0; i < 87; ++i)
        {
            // Compute parity for the current bit; inputs for parity computation
            // are the corresponding parity matrix row and each bit in the message;
            // the parity matrix row, referenced by `i`, contains 87 boolean values.
            // Each `true` value defines a message bit that must be summed, modulo
            // 2, to produce the parity check bit for the bit we're working on now.
            //
            // In short, if the parity matrix bit `(i, j)` and the message bit `j`
            // are both set, then we add 1 to the parity bits accumulator. If, after
            // processing all message bits the accumulated result is odd, then the
            // parity bit should be set for the current bit.

            std::size_t  parityBits = 0;
            std::size_t  parityByte = 0;
            std::uint8_t parityMask = 0x80;

            for (std::size_t j = 0; j < 87; ++j)
            {
                parityBits += parity(i, j) && (bytes[parityByte] & parityMask);
                parityMask  = (parityMask == 1) ? (++parityByte, 0x80) : (parityMask >> 1);
            }

            // Accumulate the parity and output bits; this is the point at which
            // we perform the modulo 2 operation on the summed parity bits.

            parityWord = (parityWord << 1) | (parityBits & 1);
            outputWord = (outputWord << 1) | ((bytes[outputByte] & outputMask) != 0);
            outputMask = (outputMask == 1) ? (++outputByte, 0x80) : (outputMask >> 1);

            // If we're at a 3-bit boundary, output the words and reset.

            if (++outputBits == 3)
            {
                *parityData++ = parityWord;
                *outputData++ = outputWord;
                parityWord    = 0;
                outputWord    = 0;
                outputBits    = 0;
            }
        }
    }
}

/******************************************************************************/
