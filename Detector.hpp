#ifndef DETECTOR_HPP__
#define DETECTOR_HPP__
#include "AudioDevice.hpp"
#include <array>
#include <vendor/Eigen/Dense>
#include <QMutex>

// Output device that distributes data in predefined chunks via a signal;
// underlying device for this abstraction is just the buffer that stores
// samples throughout a receiving period.

class Detector : public AudioDevice
{
  Q_OBJECT;

  // We downsample the input data from 48kHz to 12kHz through this
  // lowpass FIR filter.

  class Filter final
  {
  public:

    // Amount we're going to downsample; a factor of 4, i.e., 48kHz to
    // 12kHz, and number of taps in the FIR lowpass filter we're going
    // to use for the downsample process. These together result in the
    // amount to shift data in the FIR filter each time we input a new
    // sample.

    static constexpr std::size_t NDOWN = 48 / 12;
    static constexpr std::size_t NTAPS = 49;
    static constexpr std::size_t SHIFT = NTAPS - NDOWN;

    // Our FIR is constructed of a pair of Eigen vectors, each NTAPS in
    // size. Loading in a sample consists of mapping it to a read-only
    // view of an Eigen vector, NDOWN in size.

    using Vector =            Eigen::Vector<float, NTAPS>;
    using Sample = Eigen::Map<Eigen::Vector<short, NDOWN> const>;

    // Constructor; we require an array of lowpass FIR coefficients,
    // equal in size to the number of taps.

    explicit Filter(std::array<Vector::value_type, NTAPS> const & lowpass)
    : m_w(lowpass.data())
    , m_t(Vector::Zero())
    {}

    // Shift existing data in the lowpass FIR to make room for a new
    // sample and load it in; downsample through the filter.

    auto
    downSample(Sample::value_type const * const data)
    {
      m_t.head(SHIFT) = m_t.segment(NDOWN, SHIFT);
      m_t.tail(NDOWN) = Sample(data).cast<Vector::value_type>();

      return static_cast<Sample::value_type>(std::round(m_w.dot(m_t)));
    }

  private:

    // Data members

    Eigen::Map<Vector const> m_w;
    Vector                   m_t;
  };

  // Size of a maximally-sized buffer.

  static constexpr std::size_t MaxBufferSize = 7 * 512;

  // A De-interleaved sample buffer big enough for all the
  // samples for one increment of data (a signals worth) at
  // the input sample rate.

  using Buffer = std::array<short, MaxBufferSize * Filter::NDOWN>;

public:

  // Constructor

  Detector(unsigned  frameRate,
           unsigned  periodLengthInSeconds,
           QObject * parent = nullptr);

  // Inline accessors

  unsigned period() const { return m_period; }

  // Inline manipulators

  QMutex * getMutex()              { return &m_lock; }
  void     setTRPeriod(unsigned p) { m_period = p;   }

  // Accessors

  unsigned secondInPeriod () const;

  // Manipulators

  void clear();
  bool reset() override;
  void resetBufferContent();
  void resetBufferPosition();

  // Signals and slots

  Q_SIGNAL void framesWritten(qint64) const;
  Q_SLOT   void setBlockSize(unsigned);

protected:

  // We don't produce data; we're a sink for it.

  qint64 readData (char       *, qint64) override { return -1; }
  qint64 writeData(char const *, qint64) override;

private:

  // Data members

  unsigned          m_frameRate;
  unsigned          m_period;
  QMutex            m_lock;
  Filter            m_filter;
  Buffer            m_buffer;
  Buffer::size_type m_bufferPos     = 0;
  std::size_t       m_samplesPerFFT = MaxBufferSize;
  qint32            m_ns            = 999;
};

#endif
