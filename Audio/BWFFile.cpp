#include "BWFFile.hpp"

#include <cstring>
#include <numeric>
#include <algorithm>

#include <qendian.h>
#include <QAudioFormat>
#include <QDateTime>
#include <QDate>
#include <QTime>
#include <QTimeZone>
#include <QString>
#include <QUuid>

#include "pimpl_impl.hpp"

#include "moc_BWFFile.cpp"

namespace
{
  // chunk descriptor
  struct Desc
  {
    Desc ()
      : size_ {0}
    {
    }

    explicit Desc (char const * id, quint32 size = 0)
      : size_ {size}
    {
      set (id);
    }

    void set (char const * id = nullptr)
    {
      if (id)
        {
          auto end = reinterpret_cast<char const *> (::memchr (id, '\0', 4u));
          auto len = end ? end - id : 4u;
          ::memcpy (id_.data (), id, len);
          if (len < 4u)
            {
              memset (id_.data () + len, ' ', 4u - len);
            }
        }
      else
        {
          ::memcpy (id_.data (), "JUNK", 4u);
        }
    }

    void set (char const * id, quint32 size)
    {
      set (id);
      size_ = size;
    }

    char * operator & () {return reinterpret_cast<char *> (this);}
    char const * operator & () const {return reinterpret_cast<char const *> (this);}

    std::array<char, 4> id_;
    quint32 size_;
  };

  // "fmt " chunk contents
  struct FormatChunk
  {
    quint16 audio_format;
    quint16 num_channels;
    quint32 sample_rate;
    quint32 byte_rate;
    quint16 block_align;
    quint16 bits_per_sample;
  };

  // "bext" chunk contents
  struct BroadcastAudioExtension
  {
    using Version = BWFFile::BextVersion;
    using UMID = BWFFile::UMID;

    BroadcastAudioExtension (Version version = Version::v_0)
      : version_ {static_cast<quint16> (version)}
      , umid_ {{}}
    {
      // set some sensible defaults for the "bext" fields
      auto now = QDateTime::currentDateTimeUtc ();
      ::memcpy (origination_date_,
                    now.date ().toString ("yyyy-MM-dd").toLocal8Bit ().constData (),
                    sizeof origination_date_);
      ::memcpy (origination_time_,
                    now.time ().toString ("hh-mm-ss").toLocal8Bit ().constData (),
                    sizeof origination_time_);
      auto uuid = QUuid::createUuid ().toRfc4122 ();
      std::copy (uuid.cbegin (), uuid.cend (), umid_.data () + 16);
    }

    char description_[256];
    char originator_[32];
    char originator_reference_[32];
    char origination_date_[10];
    char origination_time_[10];
    quint32 time_reference_low_;
    quint32 time_reference_high_;
    quint16 version_;
    UMID umid_;                       // V1 zero for V0
    quint16 loudness_value_;          // V2
    quint16 loudness_range_;          // V2
    quint16 max_true_peak_level_;     // V2
    quint16 max_momentary_loudness_;  // V2
    quint16 max_short_term_loudness_; // V2
    quint8 reserved_[180];
    char coding_history_[];
  };
}

class BWFFile::impl final
{
public:
  using FileError = QFile::FileError;

  impl (QAudioFormat const& format)
    : header_dirty_ {true}
    , format_ {format}
    , header_length_ {-1}
    , data_size_ {-1}
    , error_ {FileError::NoError}
  {
  }

  impl (QAudioFormat const& format, QString const& name)
    : header_dirty_ {true}
    , format_ {format}
    , file_ {name}
    , header_length_ {-1}
    , data_size_ {-1}
    , error_ {FileError::NoError}
  {
  }

  impl (QAudioFormat const& format, QString const& name, InfoDictionary const& dictionary)
    : header_dirty_ {true}
    , format_ {format}
    , file_ {name}
    , info_dictionary_ {dictionary}
    , header_length_ {-1}
    , data_size_ {-1}
    , error_ {FileError::NoError}
  {
  }

  ~impl ()
  {
    if (file_.isOpen ()) file_.close ();
  }

  bool initialize (BWFFile * self, OpenMode mode);
  bool read_header ();
  bool write_header (QAudioFormat);
  bool update_header ();

  BroadcastAudioExtension const * bext () const
  {
    return bext_.isEmpty () ? nullptr : reinterpret_cast<BroadcastAudioExtension const *> (bext_.constData ());
  }

  BroadcastAudioExtension * bext ()
  {
    if (bext_.isEmpty ())       // create a "bext" chunk in place
      {
        bext_.data ();
        bext_.fill ('\0', sizeof (BroadcastAudioExtension));
        new (bext_.data ()) BroadcastAudioExtension {};
      }
    return reinterpret_cast<BroadcastAudioExtension *> (bext_.data ());
  }

  bool header_dirty_;
  QAudioFormat format_;
  QFile file_;
  QByteArray bext_;
  InfoDictionary info_dictionary_;
  qint64 header_length_;
  qint64 data_size_;
  FileError error_;
};

bool BWFFile::impl::initialize (BWFFile * self, OpenMode mode)
{
  bool result {false};
  if (mode & Append)
    {
      result = file_.seek (file_.size ());
      if (result) result = self->seek (file_.size () - header_length_);
    }
  else
    {
      result = self->seek (0);
    }
  return result;
}

bool BWFFile::impl::read_header ()
{
  header_length_ = -1;
  data_size_ = -1;
  if (!(file_.openMode () & ReadOnly)) return false;
  if (!file_.seek (0)) return false;
  Desc outer_desc;
  quint32 outer_offset = file_.pos ();
  quint32 outer_size {0};
  bool be {false};
  while (outer_offset < sizeof outer_desc + outer_desc.size_ - 1) // allow for uncounted pad
    {
      if (file_.read (&outer_desc, sizeof outer_desc) != sizeof outer_desc) return false;
      be = !memcmp (&outer_desc.id_, "RIFX", 4);
      outer_size = be ? qFromBigEndian<quint32> (outer_desc.size_) : qFromLittleEndian<quint32> (outer_desc.size_);
      if (!memcmp (&outer_desc.id_, "RIFF", 4) || be)
        {
          // RIFF or RIFX
          char riff_item[4];
          if (file_.read (riff_item, sizeof riff_item) != sizeof riff_item) return false;
          if (!memcmp (riff_item, "WAVE", 4))
            {
              // WAVE
              Desc wave_desc;
              quint32 wave_offset = file_.pos ();
              quint32 wave_size {0};
              while (wave_offset < outer_offset + sizeof outer_desc + outer_size - 1)
                {
                  if (file_.read (&wave_desc, sizeof wave_desc) != sizeof wave_desc) return false;
                  wave_size = be ? qFromBigEndian<quint32> (wave_desc.size_) : qFromLittleEndian<quint32> (wave_desc.size_);
                  if (!memcmp (&wave_desc.id_, "bext", 4))
                    {
                      bext_ = file_.read (wave_size);
                    }
                  if (!memcmp (&wave_desc.id_, "fmt ", 4))
                    {
                      FormatChunk fmt;
                      if (file_.read (reinterpret_cast<char *> (&fmt), sizeof fmt) != sizeof fmt) return false;
                      auto audio_format = be ? qFromBigEndian<quint16> (fmt.audio_format) : qFromLittleEndian<quint16> (fmt.audio_format);
                      if (audio_format != 0 && audio_format != 1) return false; // not PCM nor undefined
//                      format_.setByteOrder (be ? QSysInfo::BigEndian : QSysInfo::LittleEndian);
                      format_.setChannelCount (be ? qFromBigEndian<quint16> (fmt.num_channels) : qFromLittleEndian<quint16> (fmt.num_channels));
                      format_.setSampleRate (be ? qFromBigEndian<quint32> (fmt.sample_rate) : qFromLittleEndian<quint32> (fmt.sample_rate));
//                      int bits_per_sample {be ? qFromBigEndian<quint16> (fmt.bits_per_sample) : qFromLittleEndian<quint16> (fmt.bits_per_sample)};
                      format_.setSampleFormat(QAudioFormat::Int16);
//                      format_.setSampleSize (bits_per_sample);
//                      format_.setSampleType (8 == bits_per_sample ? QAudioFormat::UnSignedInt : QAudioFormat::SignedInt);
                    }
                  else if (!memcmp (&wave_desc.id_, "data", 4))
                    {
                      data_size_ = wave_size;
                      header_length_ = file_.pos ();
                    }
                  else if (!memcmp (&wave_desc.id_, "LIST", 4))
                    {
                      char list_type[4];
                      if (file_.read (list_type, sizeof list_type) != sizeof list_type) return false;
                      if (!memcmp (list_type, "INFO", 4))
                        {
                          Desc info_desc;
                          quint32 info_offset = file_.pos ();
                          quint32 info_size {0};
                          while (info_offset < wave_offset + sizeof wave_desc + wave_size - 1)
                            {
                              if (file_.read (&info_desc, sizeof info_desc) != sizeof info_desc) return false;
                              info_size = be ? qFromBigEndian<quint32> (info_desc.size_) : qFromLittleEndian<quint32> (info_desc.size_);
                              info_dictionary_[info_desc.id_] = file_.read (info_size);
                              if (!file_.seek (info_offset + sizeof info_desc + (info_size + 1) / 2 * 2)) return false;;
                              info_offset = file_.pos ();
                            }
                        }
                    }
                  if (!file_.seek (wave_offset + sizeof wave_desc + (wave_size + 1) / 2 * 2)) return false;
                  wave_offset = file_.pos ();
                }
            }
        }
      if (!file_.seek (outer_offset + sizeof outer_desc + (outer_size + 1) / 2 * 2)) return false;
      outer_offset = file_.pos ();
    }
  return data_size_ >= 0 && file_.seek (header_length_);
}

bool BWFFile::impl::write_header (QAudioFormat format)
{
  data_size_ = -1;
  if (!(file_.openMode () & WriteOnly)) return false;
  if (!file_.seek (0)) return false;
  header_length_ = 0;
//  bool be {QSysInfo::BigEndian == format_.byteOrder ()};
  bool be=false; // Should be OK if we only write RIFF files.
  Desc desc {be ? "RIFX" : "RIFF"};
  if (file_.write (&desc, sizeof desc) != sizeof desc) return false;
  header_dirty_ = true;
  if (file_.write ("WAVE", 4) != 4) return false;

  FormatChunk fmt;
  if (be)
    {
      fmt.audio_format = qToBigEndian<quint16> (1); // PCM
      fmt.num_channels = qToBigEndian<quint16> (format.channelCount ());
      fmt.sample_rate = qToBigEndian<quint32> (format.sampleRate ());
      fmt.byte_rate = qToBigEndian<quint32> (format.bytesForDuration (1000000));
      fmt.block_align = qToBigEndian<quint16> (format.bytesPerFrame ());
      fmt.bits_per_sample = qToBigEndian<quint16> (format.bytesPerSample () * 8);
      desc.set ("fmt", qToBigEndian<quint32> (sizeof fmt));
    }
  else
    {
      fmt.audio_format = qToLittleEndian<quint16> (1); // PCM
      fmt.num_channels = qToLittleEndian<quint16> (format.channelCount ());
      fmt.sample_rate = qToLittleEndian<quint32> (format.sampleRate ());
      fmt.byte_rate = qToLittleEndian<quint32> (format.bytesForDuration (1000000));
      fmt.block_align = qToLittleEndian<quint16> (format.bytesPerFrame ());
      fmt.bits_per_sample = qToLittleEndian<quint16> (format.bytesPerSample ()) * 8;
      desc.set ("fmt", qToLittleEndian<quint32> (sizeof fmt));
    }
  if (file_.write (&desc, sizeof desc) != sizeof desc) return false;
  if (file_.write (reinterpret_cast<char const *> (&fmt), sizeof fmt) != sizeof fmt) return false;

  desc.set ("data");
  if (file_.write (&desc, sizeof desc) != sizeof desc) return false;
  header_length_ = file_.pos ();
  return true;
}

bool BWFFile::impl::update_header ()
{
  if (header_length_ < 0 || !(file_.openMode () & WriteOnly)) return false;
  auto position = file_.pos ();
//  bool be {QSysInfo::BigEndian == format_.byteOrder ()};
  bool be=false;
  Desc desc;
  auto size = data_size_ < 0 ? file_.size () - header_length_ : data_size_;
  if (!file_.seek (header_length_ - sizeof desc)) return false;
  desc.set ("data", be ? qToBigEndian<quint32> (size) : qToLittleEndian<quint32> (size));
  if (file_.write (&desc, sizeof desc) != sizeof desc) return false;

  if (!bext_.isEmpty ())
    {
      if (!file_.seek (file_.size ())) return false;
      auto size = bext_.size ();
      desc.set ("bext", be ? qToBigEndian<quint32> (size) : qToLittleEndian<quint32> (size));
      if ((file_.size () % 2) && file_.write ("\0", 1) != 1) return false;
      if (file_.write (&desc, sizeof desc) != sizeof desc) return false;
      auto * data = reinterpret_cast<BroadcastAudioExtension *> (bext_.data ());
      if (be)
        {
          data->time_reference_low_ = qToBigEndian<quint32> (data->time_reference_low_);
          data->time_reference_high_ = qToBigEndian<quint32> (data->time_reference_high_);
          switch (static_cast<BextVersion> (data->version_))
            {
            case BextVersion::v_0:
              data->version_ = qToBigEndian<quint32> (data->version_);
              // fall through
            default:
              data->loudness_value_ = qToBigEndian<quint16> (data->loudness_value_);
              data->loudness_range_ = qToBigEndian<quint16> (data->loudness_range_);
              data->max_true_peak_level_ = qToBigEndian<quint16> (data->max_true_peak_level_);
              data->max_momentary_loudness_ = qToBigEndian<quint16> (data->max_momentary_loudness_);
              data->max_short_term_loudness_ = qToBigEndian<quint16> (data->max_short_term_loudness_);
            }
        }
      else
        {
          data->time_reference_low_ = qToLittleEndian<quint32> (data->time_reference_low_);
          data->time_reference_high_ = qToLittleEndian<quint32> (data->time_reference_high_);
          switch (static_cast<BextVersion> (data->version_))
            {
            case BextVersion::v_0:
              data->version_ = qToLittleEndian<quint32> (data->version_);
              // fall through
            default:
              data->loudness_value_ = qToLittleEndian<quint16> (data->loudness_value_);
              data->loudness_range_ = qToLittleEndian<quint16> (data->loudness_range_);
              data->max_true_peak_level_ = qToLittleEndian<quint16> (data->max_true_peak_level_);
              data->max_momentary_loudness_ = qToLittleEndian<quint16> (data->max_momentary_loudness_);
              data->max_short_term_loudness_ = qToLittleEndian<quint16> (data->max_short_term_loudness_);
            }
        }
      if (file_.write (bext_) != size) return false;
    }

  if (info_dictionary_.size ())
    {
      if (!file_.seek (file_.size ())) return false;
      desc.set ("LIST");
      if ((file_.size () % 2) && file_.write ("\0", 1) != 1) return false;
      if (file_.write (&desc, sizeof desc) != sizeof desc) return false;
      auto list_start = file_.pos ();
      if (file_.write ("INFO", 4) != 4) return false;
      for (auto iter = info_dictionary_.constBegin ()
             ; iter != info_dictionary_.constEnd (); ++iter)
        {
          auto value = iter.value ();
          auto len = value.size () + 1; // include terminating null char
          desc.set (iter.key ().data (), be ? qToBigEndian<quint32> (len) : qToLittleEndian<quint32> (len));
          if ((file_.size () % 2) && file_.write ("\0", 1) != 1) return false;
          if (file_.write (&desc, sizeof desc) != sizeof desc) return false;
          if (file_.write (value.constData (), len) != len) return false;
        }
      auto size = file_.pos () - list_start;
      if (!file_.seek (list_start - sizeof desc)) return false;
      desc.set ("LIST", be ? qToBigEndian<quint32> (size) : qToLittleEndian<quint32> (size));
      if (file_.write (&desc, sizeof desc) != sizeof desc) return false;
    }

  size = file_.size () - sizeof desc;
  if ((file_.size () % 2) && file_.seek (file_.size ()) && file_.write ("\0", 1) != 1) return false;
  if (!file_.seek (0)) return false;
  desc.set (be ? "RIFX" : "RIFF", be ? qToBigEndian<quint32> (size) : qToLittleEndian<quint32> (size));
  if (file_.write (&desc, sizeof desc) != sizeof desc) return false;
  return file_.seek (position);
}

//
// BWFFile implementation
//
BWFFile::BWFFile (QAudioFormat const& format, QObject * parent)
  : QIODevice {parent}
  , m_ {format}
{
}

BWFFile::BWFFile (QAudioFormat const& format, QString const& name, QObject * parent)
  : QIODevice {parent}
  , m_ {format, name}
{
}

BWFFile::BWFFile (QAudioFormat const& format, QString const& name
                  , InfoDictionary const& dictionary, QObject * parent)
  : QIODevice {parent}
  , m_ {format, name, dictionary}
{
}

BWFFile::~BWFFile ()
{
  if (isOpen ()) close ();
}

bool BWFFile::open (OpenMode mode)
{
  bool success {false};
  if (!(mode & WriteOnly))
    {
      if (!(success = m_->file_.open (mode & ~Text)))
        {
          setErrorString (m_->file_.errorString ());
        }
      else
        {
          if (!(success = m_->read_header ()))
            {
              if (FileError::NoError == m_->file_.error ())
                {
                  setErrorString ("Unable to read file header");
                  m_->error_ = FileError::ReadError;
                }
              else
                {
                  setErrorString (m_->file_.errorString ());
                }
            }
        }
    }
  else
    {
      if (!(success = m_->file_.open (mode & ~Text)))
        {
          setErrorString (m_->file_.errorString ());
        }
      else
        {
          if (!(success = (m_->read_header () || m_->write_header (m_->format_))))
            {
              if (FileError::NoError == m_->file_.error ())
                {
                  setErrorString ("Unable to update file header");
                  m_->error_ = FileError::WriteError;
                }
              else
                {
                  setErrorString (m_->file_.errorString ());
                }
            }
          else if (!(success = m_->file_.resize (m_->header_length_)))
            {
              setErrorString (m_->file_.errorString ());
            }
        }
    }
  success && (success = QIODevice::open (mode | Unbuffered));
  if (success && !(success = m_->initialize (this, mode)))
    {
      if (FileError::NoError == m_->file_.error ())
        {
          setErrorString ("Unable to initialize file header");
          m_->error_ = FileError::WriteError;
        }
      else
        {
          setErrorString (m_->file_.errorString ());
        }
    }
  if (!success) close ();
  return success;
}

bool BWFFile::open(FILE * fh, OpenMode mode, FileHandleFlags flags)
{
  bool success {false};
  if (!(mode & ReadOnly))
    {
      setErrorString ("Read or read/write access must be specified");
      m_->error_ = FileError::OpenError;
      return success;
    }
  if (!(mode & WriteOnly))
    {
      if (!(success = m_->file_.open (fh, mode & ~Text, flags)))
        {
          setErrorString (m_->file_.errorString ());
        }
      else
        {
          if (!(success = m_->read_header ()))
            {
              if (FileError::NoError == m_->file_.error ())
                {
                  setErrorString ("Unable to read file header");
                  m_->error_ = FileError::ReadError;
                }
              else
                {
                  setErrorString (m_->file_.errorString ());
                }
            }
        }
    }
  else
    {
      if (!(success = m_->file_.open (fh, mode & ~Text, flags)))
        {
          setErrorString (m_->file_.errorString ());
        }
      else
        {
          if (!(success = (m_->read_header () || m_->write_header (m_->format_))))
            {
              if (FileError::NoError == m_->file_.error ())
                {
                  setErrorString ("Unable to update file header");
                  m_->error_ = FileError::WriteError;
                }
              else
                {
                  setErrorString (m_->file_.errorString ());
                }
            }
          else if (!(success = m_->file_.resize (m_->header_length_)))
            {
              setErrorString (m_->file_.errorString ());
            }
        }
    }
  success && (success = QIODevice::open (mode | Unbuffered));
  if (success && !(success = m_->initialize (this, mode)))
    {
      if (FileError::NoError == m_->file_.error ())
        {
          setErrorString ("Unable to initialize file header");
          m_->error_ = FileError::WriteError;
        }
      else
        {
          setErrorString (m_->file_.errorString ());
        }
    }
  if (!success) close ();
  return success;
}

bool BWFFile::open (int fd, OpenMode mode, FileHandleFlags flags)
{
  bool success {false};
  if (!(mode & ReadOnly))
    {
      setErrorString ("Read or read/write access must be specified");
      m_->error_ = FileError::OpenError;
      return success;
    }
  if (!(mode & WriteOnly))
    {
      if (!(success = m_->file_.open (fd, mode & ~Text, flags)))
        {
          setErrorString (m_->file_.errorString ());
        }
      else
        {
          if (!(success = m_->read_header ()))
            {
              if (FileError::NoError == m_->file_.error ())
                {
                  setErrorString ("Unable to read file header");
                  m_->error_ = FileError::ReadError;
                }
              else
                {
                  setErrorString (m_->file_.errorString ());
                }
            }
        }
    }
  else
    {
      if (!(success = m_->file_.open (fd, mode & ~Text, flags)))
        {
          setErrorString (m_->file_.errorString ());
        }
      else
        {
          if (!(success = (m_->read_header () || m_->write_header (m_->format_))))
            {
              if (FileError::NoError == m_->file_.error ())
                {
                  setErrorString ("Unable to update file header");
                  m_->error_ = FileError::WriteError;
                }
              else
                {
                  setErrorString (m_->file_.errorString ());
                }
            }
          else if (!(success = m_->file_.resize (m_->header_length_)))
            {
              setErrorString (m_->file_.errorString ());
            }
        }
    }
  success && (success = QIODevice::open (mode | Unbuffered));
  if (success && !(success = m_->initialize (this, mode)))
    {
      if (FileError::NoError == m_->file_.error ())
        {
          setErrorString ("Unable to initialize file header");
          m_->error_ = FileError::WriteError;
        }
      else
        {
          setErrorString (m_->file_.errorString ());
        }
    }
  if (!success) close ();
  return success;
}

QAudioFormat const& BWFFile::format () const {return m_->format_;}

auto BWFFile::list_info () -> InfoDictionary&
{
  m_->header_dirty_ = true;
  return m_->info_dictionary_;
}


// Broadcast Audio Extension fields
auto BWFFile::bext_version () const -> BextVersion
{
  return static_cast<BextVersion> (m_->bext () ? 0 : m_->bext ()->version_);
}

void BWFFile::bext_version (BextVersion version)
{
  m_->header_dirty_ = true;
  m_->bext ()->version_ = static_cast<quint16> (version);
}

QByteArray BWFFile::bext_description () const
{
  if (!m_->bext ()) return {};
  return QByteArray::fromRawData (m_->bext ()->description_, strlen (m_->bext ()->description_));
}

void BWFFile::bext_description (QByteArray const& description)
{
  m_->header_dirty_ = true;
  ::memcpy (m_->bext ()->description_, description.constData (), sizeof BroadcastAudioExtension::description_);
}

QByteArray BWFFile::bext_originator () const
{
  if (!m_->bext ()) return {};
  return QByteArray::fromRawData (m_->bext ()->originator_, strlen (m_->bext ()->originator_));
}

void BWFFile::bext_originator (QByteArray const& originator)
{
  m_->header_dirty_ = true;
  ::memcpy (m_->bext ()->originator_, originator.constData (), sizeof BroadcastAudioExtension::originator_);
}

QByteArray BWFFile::bext_originator_reference () const
{
  if (!m_->bext ()) return {};
  return QByteArray::fromRawData (m_->bext ()->originator_reference_, strlen (m_->bext ()->originator_reference_));
}

void BWFFile::bext_originator_reference (QByteArray const& reference)
{
  m_->header_dirty_ = true;
  ::memcpy (m_->bext ()->originator_reference_, reference.constData (), sizeof BroadcastAudioExtension::originator_reference_);
}

QDateTime BWFFile::bext_origination_date_time () const
{
  if (!m_->bext ()) return {};
  return {QDate::fromString(m_->bext ()->origination_date_, "yyyy-MM-dd"),
          QTime::fromString(m_->bext ()->origination_time_, "hh-mm-ss"),
          QTimeZone::utc()};
}

void BWFFile::bext_origination_date_time (QDateTime const& dt)
{
  m_->header_dirty_ = true;
  ::memcpy (m_->bext ()->origination_date_,
                dt.date ().toString ("yyyy-MM-dd").toLocal8Bit ().constData (),
            sizeof BroadcastAudioExtension::origination_date_);
  ::memcpy (m_->bext ()->origination_time_,
                dt.time ().toString ("hh-mm-ss").toLocal8Bit ().constData (),
            sizeof BroadcastAudioExtension::origination_time_);
}

quint64 BWFFile::bext_time_reference () const
{
  if (!m_->bext ()) return 0;
  return (quint64 (m_->bext ()->time_reference_high_) << 32) + m_->bext ()->time_reference_low_;
}

void BWFFile::bext_time_reference (quint64 time_code)
{
  m_->header_dirty_ = true;
  m_->bext ()->time_reference_low_ = time_code & 0x00000000ffffffffll;
  m_->bext ()->time_reference_high_ = time_code >> 32;
}

auto BWFFile::bext_umid () const -> UMID
{
  UMID umid {{'\0'}};
  if (m_->bext ())
    {
      umid = m_->bext ()->umid_;
    }
  return umid;
}

void BWFFile::bext_umid (UMID const& umid)
{
  m_->header_dirty_ = true;
  m_->bext ()->umid_ = umid;
}

quint16 BWFFile::bext_loudness_value () const
{
  if (!m_->bext ()) return 0;
  return m_->bext ()->loudness_value_;
}

void BWFFile::bext_loudness_value (quint16 value)
{
  m_->header_dirty_ = true;
  m_->bext ()->loudness_value_ = value;
}

quint16 BWFFile::bext_loudness_range () const
{
  if (!m_->bext ()) return 0;
  return m_->bext ()->loudness_range_;
}

void BWFFile::bext_loudness_range (quint16 range)
{
  m_->header_dirty_ = true;
  m_->bext ()->loudness_range_ = range;
}

quint16 BWFFile::bext_max_true_peak_level () const
{
  if (!m_->bext ()) return 0;
  return m_->bext ()->max_true_peak_level_;
}

void BWFFile::bext_max_true_peak_level (quint16 level)
{
  m_->header_dirty_ = true;
  m_->bext ()->max_true_peak_level_ = level;
}

quint16 BWFFile::bext_max_momentary_loudness () const
{
  if (!m_->bext ()) return 0;
  return m_->bext ()->max_momentary_loudness_;
}

void BWFFile::bext_max_momentary_loudness (quint16 loudness)
{
  m_->header_dirty_ = true;
  m_->bext ()->max_momentary_loudness_ = loudness;
}

quint16 BWFFile::bext_max_short_term_loudness () const
{
  if (!m_->bext ()) return 0;
  return m_->bext ()->max_short_term_loudness_;
}

void BWFFile::bext_max_short_term_loudness (quint16 loudness)
{
  m_->header_dirty_ = true;
  m_->bext ()->max_short_term_loudness_ = loudness;
}

QByteArray BWFFile::bext_coding_history () const
{
  if (size_t (m_->bext_.size ()) <= sizeof (BroadcastAudioExtension)) return {};
  return QByteArray::fromRawData (m_->bext ()->coding_history_,
                                  m_->bext_.size () - sizeof (BroadcastAudioExtension));
}

void BWFFile::bext_coding_history (QByteArray const& text)
{
  m_->header_dirty_ = true;
  m_->bext ();                  // ensure we have a correctly
                                // initialized m_->bext_
  auto end = static_cast<char const *> (::memchr (text.constData (), '\0', size_t (text.size ())));
  auto length = end ? end - text.constData () : size_t (text.size ());
  m_->bext_.resize (sizeof (BroadcastAudioExtension) + length);
  std::strncpy (m_->bext ()->coding_history_, text.constData (), length);
}


bool BWFFile::reset ()
{
  bool success {true};
  if (m_->file_.isOpen ())
    {
      m_->info_dictionary_.clear ();
      m_->bext_.clear ();
      auto size = m_->data_size_ < 0 ? m_->file_.size () - m_->header_length_ : m_->data_size_;
      m_->data_size_ = size;
      if (m_->header_length_ > qint64 (3 * sizeof (Desc) + 4 + sizeof (FormatChunk)))
        {
          // we need to move the data down
          auto old_pos = m_->header_length_;
          if (!(success = m_->write_header (m_->format_)))
            {
              auto new_pos = m_->header_length_;
              QByteArray buffer;
              while (size)
                {
                  success = m_->file_.seek (old_pos);
                  if (!success)
                    {
                      setErrorString (m_->file_.errorString ());
                    }
                  else
                    {
                      buffer = m_->file_.read (std::min (size, qint64 (32768)));
                      success = m_->file_.seek (new_pos);
                      if (!success)
                        {
                          setErrorString (m_->file_.errorString ());
                        }
                      else
                        {
                          qint64 bytes = m_->file_.write (buffer);
                          if (bytes < 0)
                            {
                              setErrorString (m_->file_.errorString ());
                              success = false;
                            }
                          else
                            {
                              new_pos += buffer.size ();
                              old_pos += buffer.size ();
                              size -= buffer.size ();
                            }
                        }
                    }
                }
            }
          else
            {
              if (FileError::NoError == m_->file_.error ())
                {
                  setErrorString ("Unable to update file header");
                  m_->error_ = FileError::WriteError;
                }
              else
                {
                  setErrorString (m_->file_.errorString ());
                }
              return success;
            }
        }
      if (success)
        {
          success = m_->file_.resize (m_->header_length_ + m_->data_size_);
          if (!success)
            {
              setErrorString (m_->file_.errorString ());
            }
          m_->header_dirty_ = true;
        }
    }
  else
    {
      setErrorString ("File not open");
      m_->error_ = FileError::WriteError;
      success = false;
    }
  return success && QIODevice::reset ();
}

qint64 BWFFile::size () const
{
  return m_->data_size_ < 0 ? m_->file_.size () - m_->header_length_ : m_->data_size_;
}

bool BWFFile::isSequential () const
{
  return m_->file_.isSequential ();
}

void BWFFile::close ()
{
  if (isOpen ()) QIODevice::close ();
  if (m_->header_dirty_ || m_->data_size_ < 0)
    {
      m_->update_header ();
    }
  if (m_->file_.isOpen ())
    {
      m_->file_.close ();
    }
}

bool BWFFile::seek (qint64 pos)
{
  if (pos < 0)
    {
      setErrorString ("Attempt to seek before beginning of data");
      m_->error_ = FileError::PositionError;
      return false;
    }
  bool success = QIODevice::seek (pos);
  if (success)
    {
      success = m_->file_.seek (pos + m_->header_length_);
      if (!success) setErrorString (m_->file_.errorString ());
    }
  return success;
}

qint64 BWFFile::readData (char * data, qint64 max_size)
{
  return m_->file_.read (data, max_size);
}

qint64 BWFFile::writeData (char const* data, qint64 max_size)
{
  auto bytes = m_->file_.write (data, max_size);
  if (bytes > 0 && atEnd ())
    {
      m_->header_dirty_ = true;
      m_->data_size_ = -1;
    }
  return bytes;
}

// forward to QFile
bool BWFFile::copy (QString const& new_name)
{
  close ();
  return m_->file_.copy (new_name);
}

bool BWFFile::exists () const {return m_->file_.exists ();}

bool BWFFile::link (QString const& link_name) {return m_->file_.link (link_name);}

bool BWFFile::remove ()
{
  close ();
  return m_->file_.remove ();
}

bool BWFFile::rename (QString const& new_name)
{
  close ();
  return m_->file_.rename (new_name);
}

void BWFFile::setFileName (QString const& name) {m_->file_.setFileName (name);}

QString BWFFile::symLinkTarget () const {return m_->file_.symLinkTarget ();}

QString BWFFile::fileName () const {return m_->file_.fileName ();}

auto BWFFile::permissions () const -> Permissions {return m_->file_.permissions ();}

bool BWFFile::resize (qint64 new_size)
{
  auto size = m_->file_.size ();
  if (pos () > new_size) seek (new_size);
  auto result = m_->file_.resize (m_->header_length_ + new_size);
  if (m_->data_size_ >= 0)
    {
      // set any fresh bytes to zero
      auto end_of_data = m_->header_length_ + m_->data_size_;
      auto length = std::min (size - end_of_data, m_->file_.size () - end_of_data);
      if (length > 0)
        {
          auto position = m_->file_.pos ();
          m_->file_.seek (m_->header_length_ + m_->data_size_);
          m_->file_.write (QByteArray {int (length), '\0'});
          m_->file_.seek (position);
        }
      m_->data_size_ = -1;
    }
  m_->header_dirty_ = true;
  return result;
}

bool BWFFile::setPermissions (Permissions permissions)
{
  bool success = m_->file_.setPermissions (permissions);
  if (!success) setErrorString (m_->file_.errorString ());
  return success;
}

auto BWFFile::error () const -> FileError
{
  return m_->error_ != FileError::NoError ? m_->error_ : m_->file_.error ();
}

bool BWFFile::flush ()
{
  bool success = m_->file_.flush ();
  if (!success) setErrorString (m_->file_.errorString ());
  return success;
}

int BWFFile::handle () const
{
  int h {m_->file_.handle ()};
  if (h < 0) const_cast<BWFFile *> (this)->setErrorString (m_->file_.errorString ());
  return h;
}

uchar * BWFFile::map (qint64 offset, qint64 size, MemoryMapFlags flags)
{
  uchar * address = m_->file_.map (offset + m_->header_length_, size, flags);
  if (!address) setErrorString (m_->file_.errorString ());
  return address;
}

bool BWFFile::unmap (uchar * address)
{
  bool success = m_->file_.unmap (address);
  if (!success) setErrorString (m_->file_.errorString ());
  return success;
}

void BWFFile::unsetError ()
{
  m_->error_ = FileError::NoError;
  m_->file_.unsetError ();
}
