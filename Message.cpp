/**
 * This file is part of JS8Call.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * (C) 2018 Jordan Sherer <kn4crd@gmail.com> - All Rights Reserved
 *
 **/

#include "Message.hpp"
#include "MessageError.hpp"
#include "DriftingDateTime.h"

/******************************************************************************/
// Constants
/******************************************************************************/

namespace
{
  constexpr qint64 EPOCH = 1499299200000; // July 6, 2017
}

/******************************************************************************/
// Local Utilities
/******************************************************************************/

namespace
{
  auto
  generateId()
  {
    return QString::number(DriftingDateTime::currentMSecsSinceEpoch() - EPOCH);
  }
}

/******************************************************************************/
// Message::Data Implementation
/******************************************************************************/

struct Message::Data final : public QSharedData
{
  // Return the ID value from the variant map, if there is one, or zero
  // if there's no ID in the map.

  qint64
  id() const
  {
    return params_.value("_ID").toLongLong(); 
  }

  // Generate a new ID value and insert it into the map, replacing any ID
  // value that might already have been present. Return the ID value.

  qint64
  insertId()
  {
    return params_.insert("_ID", generateId())->toLongLong();
  }

  // If there's a non-zero ID in the variant map, return it, otherwise
  // generate one, insert it, and return it.

  qint64
  ensureId()
  {
    if (auto const it  = params_.find("_ID");
                   it != params_.end())
    {
      if (auto const id = it->toLongLong()) return id;
    }

    return insertId();
  }

  // Data members

  QString     type_;
  QString     value_;
  QVariantMap params_;
};

/******************************************************************************/
// Constructors
/******************************************************************************/

Message::Message()
: d_ {new Data}
{}

Message::Message(QString const & type,
                 QString const & value)
: Message()
{
  d_->type_  = type,
  d_->value_ = value;
  d_->insertId();
}

Message::Message(QString     const & type,
                 QString     const & value,
                 QVariantMap const & params)
: Message()
{
  d_->type_   = type,
  d_->value_  = value;
  d_->params_ = params;
  d_->ensureId();
}

/******************************************************************************/
// Assignment, copying, and destruction
/******************************************************************************/

Message & Message::operator=(Message       &&) noexcept = default;
Message & Message::operator=(Message const & )          = default;
Message::Message            (Message const & )          = default;
Message::Message            (Message       &&) noexcept = default;
Message::~Message()                                     = default;

/******************************************************************************/
// Accessors
/******************************************************************************/

qint64
Message::id() const
{
  return d_->id();
}

QString
Message::type() const
{
  return d_->type_;
}

QString
Message::value() const
{
  return d_->value_;
}

QVariantMap
Message::params() const
{
  return d_->params_;
}

/******************************************************************************/
// Manipulators
/******************************************************************************/

qint64
Message::ensureId()
{
  return d_->ensureId();
}

void
Message::setType(QString const & type)
{
  d_->type_ = type;
}

void
Message::setValue(QString const & value)
{
  d_->value_ = value;
}

/******************************************************************************/
// Deserialization
/******************************************************************************/

Message
Message::fromJson(QByteArray const & json)
{
  QJsonParseError parse;
  QJsonDocument   document = QJsonDocument::fromJson(json, &parse);

  if (parse.error) throw std::system_error
  {
    MessageError::Code::json_parsing_error,
    parse.errorString().toStdString()
  };

  return fromJson(document);
}

Message
Message::fromJson(QJsonDocument const & json)
{
  if (!json.isObject()) throw std::system_error
  {
    MessageError::Code::json_not_an_object
  };

  return fromJson(json.object());
}

Message
Message::fromJson(QJsonObject const & json)
{
  Message message;

  if (auto const it  = json.find("type");
                 it != json.end() && it->isString())
  {
    message.d_->type_ = it->toString();
  }

  if (auto const it = json.find("value");
                 it != json.end() && it->isString())
  {
    message.d_->value_ = it->toString();
  }

  if (auto const it  = json.find("params");
                 it != json.end() && it->isObject())
  {
    message.d_->params_ = it->toObject().toVariantMap();
  }

  return message;
}

/******************************************************************************/
// Conversions
/******************************************************************************/

QByteArray
Message::toJson() const
{
  return toJsonDocument().toJson(QJsonDocument::Compact);
}

QJsonDocument
Message::toJsonDocument() const
{
  return QJsonDocument(toJsonObject());
}

QJsonObject
Message::toJsonObject() const
{
  return {
    { "type",                               d_->type_    },
    { "value",                              d_->value_   },
    { "params", QJsonObject::fromVariantMap(d_->params_) }
  };
}

QVariantMap
Message::toVariantMap() const
{
  return {
    { "type",   d_->type_   },
    { "value",  d_->value_  },
    { "params", d_->params_ }
  };
}

/******************************************************************************/
