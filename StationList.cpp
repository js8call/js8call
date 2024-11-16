#include "StationList.hpp"

#include <utility>
#include <algorithm>
#include <cmath>

#include <QMetaType>
#include <QAbstractTableModel>
#include <QObject>
#include <QString>
#include <QVector>
#include <QStringList>
#include <QMimeData>
#include <QDataStream>
#include <QByteArray>
#include <QDebug>
#include <QDebugStateSaver>
#include <QTimeZone>

#include "DriftingDateTime.h"

#include "pimpl_impl.hpp"

#include "Radio.hpp"
#include "Bands.hpp"
#include "FrequencyList.hpp"

#if !defined (QT_NO_DEBUG_STREAM)
QDebug operator << (QDebug debug, StationList::Station const& station)
{
  QDebugStateSaver saver {debug};
  debug.nospace () << "Station("
                   << station.band_name_ << ", "
                   << station.frequency_ << ", "
                   << station.switch_at_ << ", "
                   << station.switch_until_ << ", "
                   << station.description_ << ')';
  return debug;
}
#endif

QDataStream& operator << (QDataStream& os, StationList::Station const& station)
{
  return os << station.band_name_
            << station.frequency_
            << station.switch_at_
            << station.switch_until_
            << station.description_;
}

QDataStream& operator >> (QDataStream& is, StationList::Station& station)
{
  return is >> station.band_name_
            >> station.frequency_
            >> station.switch_at_
            >> station.switch_until_
            >> station.description_;
}


class StationList::impl final
  : public QAbstractTableModel
{
public:
  impl (Bands const * bands, Stations stations, QObject * parent)
    : QAbstractTableModel {parent}
    , bands_ {bands}
    , stations_ {stations}
  {
  }

  Stations station_list (Stations);
  QModelIndex add (Station);
  FrequencyDelta offset (Frequency) const;

  // Implement the QAbstractTableModel interface.
  int rowCount (QModelIndex const& parent = QModelIndex {}) const override;
  int columnCount (QModelIndex const& parent = QModelIndex {}) const override;
  Qt::ItemFlags flags (QModelIndex const& = QModelIndex {}) const override;
  QVariant data (QModelIndex const&, int role) const override;
  QVariant headerData (int section, Qt::Orientation, int = Qt::DisplayRole) const override;
  bool setData (QModelIndex const&, QVariant const& value, int role = Qt::EditRole) override;
  bool removeRows (int row, int count, QModelIndex const& parent = QModelIndex {}) override;
  bool insertRows (int row, int count, QModelIndex const& parent = QModelIndex {}) override;
  Qt::DropActions supportedDropActions () const override;
  QStringList mimeTypes () const override;
  QMimeData * mimeData (QModelIndexList const&) const override;
  bool dropMimeData (QMimeData const *, Qt::DropAction, int row, int column, QModelIndex const& parent) override;

  // Helper method for band validation.
  QModelIndex first_matching_band (QString const& band_name) const
  {
    // find first exact match in bands
    auto matches = bands_->match (bands_->index (0, 0)
                                  , Qt::DisplayRole
                                  , band_name
                                  , 1
                                  , Qt::MatchExactly);
    return matches.isEmpty () ? QModelIndex {} : matches.first ();
  }

  static int constexpr num_columns {5};
  static auto constexpr mime_type = "application/wsjt.antenna-descriptions";

  Bands const * bands_;
  Stations stations_;
};

StationList::StationList (Bands const * bands, QObject * parent)
  : StationList {bands, {}, parent}
{
}

StationList::StationList (Bands const * bands, Stations stations, QObject * parent)
  : QSortFilterProxyModel {parent}
  , m_ {bands, stations, parent}
{
  setSourceModel (&*m_);
  setSortRole (SortRole);
}

StationList::~StationList ()
{
}

auto StationList::station_list (Stations stations) -> Stations
{
  return m_->station_list (stations);
}

auto StationList::station_list () const -> Stations const&
{
  return m_->stations_;
}

QModelIndex StationList::add (Station s)
{
  return mapFromSource (m_->add (s));
}

bool StationList::remove (Station s)
{
  auto row = m_->stations_.indexOf (s);
  if (0 > row)
    {
      return false;
    }
  return removeRow (row);
}

namespace
{
  bool row_is_higher (QModelIndex const& lhs, QModelIndex const& rhs)
  {
    return lhs.row () > rhs.row ();
  }
}

bool StationList::removeDisjointRows (QModelIndexList rows)
{
  bool result {true};

  // We must work with source model indexes because we don't want row
  // removes to invalidate model indexes we haven't yet processed. We
  // achieve that by processing them in decending row order.
  for (int r = 0; r < rows.size (); ++r)
    {
      rows[r] = mapToSource (rows[r]);
    }

  // reverse sort by row
  std::sort (rows.begin (), rows.end (), row_is_higher);
  Q_FOREACH (auto index, rows)
    {
      if (result && !m_->removeRow (index.row ()))
        {
          result = false;
        }
    }

  return result;
}

auto StationList::impl::station_list (Stations stations) -> Stations
{
  beginResetModel ();
  std::swap (stations_, stations);
  endResetModel ();
  return stations;
}

QModelIndex StationList::impl::add (Station s)
{
  // Any band that isn't in the list may be added
  if (!stations_.contains (s))
    {
      auto row = stations_.size ();

      beginInsertRows (QModelIndex {}, row, row);
      stations_.append (s);
      endInsertRows ();

      return index (row, 0);
    }

  return QModelIndex {};
}

#if 0
auto StationList::impl::offset (Frequency f) const -> FrequencyDelta
{
  // Lookup band for frequency
  auto const& band = bands_->find (f);
  if (!band.isEmpty ())
    {
      // Lookup station for band
      for (int i = 0; i < stations_.size (); ++i)
        {
          if (stations_[i].band_name_ == band)
            {
              return stations_[i].frequency_;
            }
        }
    }
  return 0;                     // no offset
}
#endif

int StationList::impl::rowCount (QModelIndex const& parent) const
{
  return parent.isValid () ? 0 : stations_.size ();
}

int StationList::impl::columnCount (QModelIndex const& parent) const
{
  return parent.isValid () ? 0 : num_columns;
}

Qt::ItemFlags StationList::impl::flags (QModelIndex const& index) const
{
  auto result = QAbstractTableModel::flags (index);

  auto row = index.row ();
  auto column = index.column ();

  if (index.isValid ()
      && row < stations_.size ()
      && column < num_columns)
    {
      if (band_column == column){
          result |= Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled;

      } else if (description_column == column)
        {
          result |= Qt::ItemIsEditable | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled;
        }
      else
        {
          result |= Qt::ItemIsEditable | Qt::ItemIsDropEnabled;
        }
    }
  else
    {
      result |= Qt::ItemIsDropEnabled;
    }

  return result;
}

QVariant StationList::impl::data (QModelIndex const& index, int role) const
{
  QVariant item;

  auto row = index.row ();
  auto column = index.column ();

  if (index.isValid ()
      && row < stations_.size ())
    {
      switch (column)
        {
        case band_column:
          switch (role)
            {
            case SortRole:
              {
                // Lookup band.
                auto band_index = first_matching_band (stations_.at (row).band_name_);
                // Use the sort role value of the band.
                item = band_index.data (Bands::SortRole);
              }
              break;

            case Qt::DisplayRole:
            case Qt::EditRole:
            case Qt::AccessibleTextRole:
              item = stations_.at (row).band_name_;
              break;

            case Qt::ToolTipRole:
            case Qt::AccessibleDescriptionRole:
              item = tr ("Band name");
              break;

            case Qt::TextAlignmentRole:
              item = Qt::AlignCenter;
              break;
            }
          break;

        case frequency_column:
          {
            auto frequency_offset = stations_.at (row).frequency_;
            switch (role)
              {
              case SortRole:
              case Qt::EditRole:
              case Qt::AccessibleTextRole:
                item = frequency_offset;
                break;

              case Qt::DisplayRole:
                item = Radio::pretty_frequency_MHz_string (frequency_offset) + " MHz";
                break;

              case Qt::ToolTipRole:
              case Qt::AccessibleDescriptionRole:
                item = tr ("Frequency");
                break;

              case Qt::TextAlignmentRole:
                item = static_cast<Qt::Alignment::Int>(Qt::AlignRight | Qt::AlignVCenter);
                break;
              }
          }
          break;

      case switch_at_column:
        switch (role)
          {
          case SortRole:
          case Qt::EditRole:
          case Qt::DisplayRole:
          case Qt::AccessibleTextRole:
            item = stations_.at (row).switch_at_.time().toString("hh:mm");
            break;

          case Qt::ToolTipRole:
          case Qt::AccessibleDescriptionRole:
            item = tr ("Switch at this time");
            break;

          case Qt::TextAlignmentRole:
            item = Qt::AlignCenter;
            break;
          }
        break;

      case switch_until_column:
        switch (role)
          {
          case SortRole:
          case Qt::EditRole:
          case Qt::DisplayRole:
          case Qt::AccessibleTextRole:
            item = stations_.at (row).switch_until_.time().toString("hh:mm");
            break;

          case Qt::ToolTipRole:
          case Qt::AccessibleDescriptionRole:
            item = tr ("Switch until this time");
            break;

          case Qt::TextAlignmentRole:
            item = Qt::AlignCenter;
            break;
          }
        break;

      case description_column:
        switch (role)
          {
          case SortRole:
          case Qt::EditRole:
          case Qt::DisplayRole:
          case Qt::AccessibleTextRole:
            item = stations_.at (row).description_;
            break;

          case Qt::ToolTipRole:
          case Qt::AccessibleDescriptionRole:
            item = tr ("Antenna description");
            break;

          case Qt::TextAlignmentRole:
            item = static_cast<Qt::Alignment::Int>(Qt::AlignLeft | Qt::AlignVCenter);
            break;
          }
        break;
      }
    }

  return item;
}

QVariant StationList::impl::headerData (int section, Qt::Orientation orientation, int role) const
{
  QVariant header;

  if (Qt::DisplayRole == role && Qt::Horizontal == orientation)
    {
      switch (section)
        {
        case band_column: header = tr ("Band"); break;
        case frequency_column: header = tr ("Freq. (MHz)"); break;
        case switch_at_column: header = tr ("Switch at (UTC)"); break;
        case switch_until_column: header = tr ("Until (UTC)"); break;
        case description_column: header = tr ("Description"); break;
        }
    }
  else
    {
      header = QAbstractTableModel::headerData (section, orientation, role);
    }

  return header;
}

bool StationList::impl::setData (QModelIndex const& model_index, QVariant const& value, int role)
{
  bool changed {false};

  auto row = model_index.row ();
  auto size = stations_.size ();
  if (model_index.isValid ()
      && Qt::EditRole == role
      && row < size)
    {
      QVector<int> roles;
      roles << role;

      switch (model_index.column ())
        {
        case band_column:
          {
            // Check if band name is valid.
            auto band_index = first_matching_band (value.toString ());
            if (band_index.isValid ())
              {
                stations_[row].band_name_ = band_index.data ().toString ();
                Q_EMIT dataChanged (model_index, model_index, roles);
                changed = true;
              }
          }
          break;

        case frequency_column:
          {
            if (value.canConvert<Frequency> ())
              {
                Frequency offset {qvariant_cast<Radio::Frequency> (value)};

                if(offset == 0){
                    Frequency l;
                    Frequency h;
                    if(bands_->findFreq(stations_[row].band_name_, &l, &h)){
                        offset = l;
                    }
                }

                if (offset != stations_[row].frequency_)
                  {
                    stations_[row].frequency_ = offset;
                    Q_EMIT dataChanged (model_index, model_index, roles);

                    auto band = bands_->find(offset);
                    if(band != stations_[row].band_name_){
                        stations_[row].band_name_ = band;
                        auto band_index = model_index.model()->index(row, band_column, model_index);
                        Q_EMIT dataChanged (band_index, band_index, roles);
                    }
                    changed = true;
                  }
              }
          }
          break;
        case switch_at_column:
        {
          QString s = value.toString();
          if(s.length() < 5){
              s = QString("0").repeated(5-s.length()) + s;
          }
          auto t = QTime::fromString(s);
          auto at = QDateTime(QDate(2000,1,1), t, QTimeZone::utc());
          auto until = stations_[row].switch_until_;
          stations_[row].switch_at_ = at;
          stations_[row].switch_until_ = until;

          Q_EMIT dataChanged (model_index, model_index, roles);

          auto switch_until_index = model_index.model()->index(row, switch_until_column, model_index);
          Q_EMIT dataChanged (switch_until_index, switch_until_index, roles);

          changed = true;
          break;
        }
        case switch_until_column:
        {
          QString s = value.toString();
          if(s.length() < 5){
              s = QString("0").repeated(5-s.length()) + s;
          }
          auto t = QTime::fromString(s);
          auto until = QDateTime(QDate(2000,1,1), t, QTimeZone::utc());
          auto at = stations_[row].switch_at_;
          stations_[row].switch_at_ = at;
          stations_[row].switch_until_ = until;

          Q_EMIT dataChanged (model_index, model_index, roles);

          auto switch_at_index = model_index.model()->index(row, switch_at_column, model_index);
          Q_EMIT dataChanged (switch_at_index, switch_at_index, roles);

          changed = true;
          break;
        }
        case description_column:
          stations_[row].description_ = value.toString ();
          Q_EMIT dataChanged (model_index, model_index, roles);
          changed = true;
          break;
        }
    }

  return changed;
}

bool StationList::impl::removeRows (int row, int count, QModelIndex const& parent)
{
  if (0 < count && (row + count) <= rowCount (parent))
    {
      beginRemoveRows (parent, row, row + count - 1);
      for (auto r = 0; r < count; ++r)
        {
          stations_.removeAt (row);
        }
      endRemoveRows ();
      return true;
    }

  return false;
}

bool StationList::impl::insertRows (int row, int count, QModelIndex const& parent)
{
  if (0 < count)
    {
      beginInsertRows (parent, row, row + count - 1);
      for (auto r = 0; r < count; ++r)
        {
          stations_.insert (row, Station ());
        }
      endInsertRows ();
      return true;
    }

  return false;
}

Qt::DropActions StationList::impl::supportedDropActions () const
{
  return Qt::CopyAction | Qt::MoveAction;
}

QStringList StationList::impl::mimeTypes () const
{
  QStringList types;
  types << mime_type;
  types << "application/wsjt.Frequencies";
  return types;
}

QMimeData * StationList::impl::mimeData (QModelIndexList const& items) const
{
  QMimeData * mime_data = new QMimeData {};
  QByteArray encoded_data;
  QDataStream stream {&encoded_data, QIODevice::WriteOnly};

  Q_FOREACH (auto const& item, items)
    {
      if (item.isValid ())
        {
          stream << QString {data (item, Qt::DisplayRole).toString ()};
        }
    }

  mime_data->setData (mime_type, encoded_data);
  return mime_data;
}

bool StationList::impl::dropMimeData (QMimeData const * data, Qt::DropAction action, int /* row */, int /* column */, QModelIndex const& parent)
{
  if (Qt::IgnoreAction == action)
    {
      return true;
    }
  if (parent.isValid ()
      && description_column == parent.column ()
      && data->hasFormat (mime_type))
    {
      QByteArray encoded_data {data->data (mime_type)};
      QDataStream stream {&encoded_data, QIODevice::ReadOnly};
      auto dest_index = parent;
      while (!stream.atEnd ())
        {
          QString text;
          stream >> text;
          setData (dest_index, text);
          dest_index = index (dest_index.row () + 1, dest_index.column (), QModelIndex {});
        }
      return true;
    }
  else if (data->hasFormat ("application/wsjt.Frequencies"))
    {
      QByteArray encoded_data {data->data ("application/wsjt.Frequencies")};
      QDataStream stream {&encoded_data, QIODevice::ReadOnly};
      while (!stream.atEnd ())
        {
          FrequencyList_v2::Item item;
          stream >> item;
          auto const& band = bands_->find (item.frequency_);
          if (stations_.cend () == std::find_if (stations_.cbegin ()
                                                 , stations_.cend ()
                                                 , [&band] (Station const& s) {return s.band_name_ == band;}))
            {
              // not found so add it
              add (Station {band, 0, DriftingDateTime::currentDateTimeUtc(), DriftingDateTime::currentDateTimeUtc(), QString {}});
            }
        }
      return true;
    }
  return false;
}
