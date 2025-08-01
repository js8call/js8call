#ifndef __JS8
#define __JS8

#include <array>
#include <functional>
#include <string>
#include <variant>
#include <QObject>
#include <QSemaphore>
#include <QThread>

namespace JS8
{
  Q_NAMESPACE

  namespace Costas
  {
    // JS8 originally used the same Costas arrays as FT8 did, and so
    // that's still the array in use by 'normal' mode. All the other
    // modes use the modified arrays.

    enum class Type
    {
      ORIGINAL,
      MODIFIED
    };

    using Array = std::array<std::array<int, 7>, 3>;

    constexpr auto array = []
    {
      constexpr auto COSTAS = std::array
      {
        std::array
        {
          std::array{4, 2, 5, 6, 1, 3, 0},
          std::array{4, 2, 5, 6, 1, 3, 0},
          std::array{4, 2, 5, 6, 1, 3, 0}
        },
        std::array
        {
          std::array{0, 6, 2, 3, 5, 4, 1},
          std::array{1, 5, 0, 2, 3, 6, 4},
          std::array{2, 5, 0, 6, 4, 1, 3}
        }
      };

      return [COSTAS](Type type) -> Array const &
      {
        return COSTAS[static_cast<std::underlying_type_t<Type>>(type)];
      };
    }();
  }

  void
  encode(int                   type,
         Costas::Array const & costas,
         const char          * message,
         int                 * tones);

  namespace Event
  {
    struct DecodeStarted
    {
      int submodes;
    };

    struct SyncStart
    {
      int position;
      int size;
    };

    struct SyncState
    {
      enum class Type
      {
        CANDIDATE,
        DECODED
      } type;
      int   mode;
      float frequency;
      float dt;
      union {
        int   candidate;
        float decoded;
      } sync;
    };

    struct Decoded
    {
      int         utc;  // you can use the output of code_time() from commons.h here.
      int         snr;
      float       xdt;
      float       frequency;
      std::string data;
      int         type;
      float       quality;
      int         mode;
    };

    struct DecodeFinished
    {
      int decoded;
    };

    using Variant = std::variant<DecodeStarted,
                                 SyncStart,
                                 SyncState,
                                 Decoded,
                                 DecodeFinished>;

    using Emitter = std::function<void(Variant const &)>;
  }

  class Worker;

  class Decoder: public QObject
  {
    Q_OBJECT

    QSemaphore m_semaphore;
    QThread    m_thread;
    Worker   * m_worker;

  public:
      
    Decoder(QObject * parent = nullptr);

  signals:

      void decodeEvent(Event::Variant const &);

  public slots:

    void start(QThread::Priority priority);
    void quit();
    void decode();
  };
}

#endif
