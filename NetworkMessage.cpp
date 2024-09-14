#include "NetworkMessage.hpp"

#include <exception>

#include <QString>
#include <QByteArray>
#include <QDebug>
#include <QIODevice>

#include "pimpl_impl.hpp"

namespace NetworkMessage
{
  Builder::Builder (QIODevice * device, Type type, QString const& id, quint32 schema)
    : QDataStream {device}
  {
    common_initialization (type, id, schema);
  }

  Builder::Builder (QByteArray * a, Type type, QString const& id, quint32 schema)
    : QDataStream {a, QIODevice::WriteOnly}
  {
    common_initialization (type, id, schema);
  }

  void Builder::common_initialization (Type type, QString const& id, quint32 schema)
  {
    if (schema <= 1)
      {
        setVersion (QDataStream::Qt_5_0); // Qt schema version
      }
#if QT_VERSION >= 0x050200
    else if (schema <= 2)
      {
        setVersion (QDataStream::Qt_5_2); // Qt schema version
      }
#endif
#if QT_VERSION >= 0x050400
    else if (schema <= 3)
      {
        setVersion (QDataStream::Qt_5_4); // Qt schema version
      }
#endif
    else
      {
        throw std::runtime_error {"Unrecognized message schema"};
      }

    // the following two items assume that the quint32 encoding is
    // unchanged over QDataStream versions
    *this << magic;
    *this << schema;

    *this << static_cast<quint32> (type) << id.toUtf8 ();
  }

  class Reader::impl
  {
  public:
    void common_initialization (Reader * parent)
    {
      quint32 magic;
      *parent >> magic;
      if (magic != Builder::magic)
        {
          throw std::runtime_error {"Invalid message format"};
        }
      *parent >> schema_;
      if (schema_ > Builder::schema_number)
        {
          throw std::runtime_error {"Unrecognized message schema"};
        }
      if (schema_ <= 1)
        {
          parent->setVersion (QDataStream::Qt_5_0);
        }
#if QT_VERSION >= 0x050200
      else if (schema_ <= 2)
        {
          parent->setVersion (QDataStream::Qt_5_2);
        }
#endif
#if QT_VERSION >= 0x050400
      else if (schema_ <= 3)
        {
          parent->setVersion (QDataStream::Qt_5_4);
        }
#endif
      quint32 type;
      *parent >> type >> id_;
      if (type >= maximum_message_type_)
        {
          qDebug () << "Unrecognized message type:" << type << "from id:" << id_;
          type_ = maximum_message_type_;
        }
      else
        {
          type_ = static_cast<Type> (type);
        }
    }

    quint32 schema_;
    Type type_;
    QByteArray id_;
  };

  Reader::Reader (QIODevice * device)
    : QDataStream {device}
  {
    m_->common_initialization (this);
  }

  Reader::Reader (QByteArray const& a)
    : QDataStream {a}
  {
    m_->common_initialization (this);
  }

  Reader::~Reader ()
  {
  }

  quint32 Reader::schema () const
  {
    return m_->schema_;
  }

  Type Reader::type () const
  {
    return static_cast<Type> (m_->type_);
  }

  QString Reader::id () const
  {
    return QString::fromUtf8 (m_->id_);
  }
}
