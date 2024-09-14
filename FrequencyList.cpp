#include "FrequencyList.hpp"

#include <cstdlib>
#include <utility>
#include <limits>

#include <QMetaType>
#include <QAbstractTableModel>
#include <QString>
#include <QList>
#include <QListIterator>
#include <QVector>
#include <QStringList>
#include <QMimeData>
#include <QDataStream>
#include <QByteArray>
#include <QDebugStateSaver>

#include "Radio.hpp"
#include "Bands.hpp"
#include "pimpl_impl.hpp"

#include "moc_FrequencyList.cpp"

namespace
{
  FrequencyList_v2::FrequencyItems const default_frequency_list =
    {
      {  1842000, Modes::JS8, IARURegions::ALL}, // 2 above
      {  3578000, Modes::JS8, IARURegions::ALL}, // 5 above
      {  7078000, Modes::JS8, IARURegions::ALL}, // 4 above
      { 10130000, Modes::JS8, IARURegions::ALL}, // 6 below
      { 14078000, Modes::JS8, IARURegions::ALL}, // 4 above
      { 18104000, Modes::JS8, IARURegions::ALL}, // 4 above
      { 21078000, Modes::JS8, IARURegions::ALL}, // 4 above
      { 24922000, Modes::JS8, IARURegions::ALL}, // 9 above
      { 28078000, Modes::JS8, IARURegions::ALL}, // 4 above
      { 50318000, Modes::JS8, IARURegions::ALL}, // 5 above
      {144178000, Modes::JS8, IARURegions::ALL}, // 4 above
    };
}

#if !defined (QT_NO_DEBUG_STREAM)
QDebug operator << (QDebug debug, FrequencyList_v2::Item const& item)
{
  QDebugStateSaver saver {debug};
  debug.nospace () << "FrequencyItem("
                   << item.frequency_ << ", "
                   << item.region_ << ", "
                   << item.mode_ << ')';
  return debug;
}
#endif

QDataStream& operator << (QDataStream& os, FrequencyList_v2::Item const& item)
{
  return os << item.frequency_
            << item.mode_
            << item.region_;
}

QDataStream& operator >> (QDataStream& is, FrequencyList_v2::Item& item)
{
  return is >> item.frequency_
            >> item.mode_
            >> item.region_;
}

class FrequencyList_v2::impl final
  : public QAbstractTableModel
{
public:
  impl (Bands const * bands, QObject * parent)
    : QAbstractTableModel {parent}
    , bands_ {bands}
    , region_filter_ {IARURegions::ALL}
    , mode_filter_ {Modes::ALL}
  {
  }

  FrequencyItems frequency_list (FrequencyItems);
  QModelIndex add (Item);
  void add (FrequencyItems);

  // Implement the QAbstractTableModel interface
  int rowCount (QModelIndex const& parent = QModelIndex {}) const override;
  int columnCount (QModelIndex const& parent = QModelIndex {}) const override;
  Qt::ItemFlags flags (QModelIndex const& = QModelIndex {}) const override;
  QVariant data (QModelIndex const&, int role = Qt::DisplayRole) const override;
  bool setData (QModelIndex const&, QVariant const& value, int role = Qt::EditRole) override;
  QVariant headerData (int section, Qt::Orientation, int = Qt::DisplayRole) const override;
  bool removeRows (int row, int count, QModelIndex const& parent = QModelIndex {}) override;
  bool insertRows (int row, int count, QModelIndex const& parent = QModelIndex {}) override;
  QStringList mimeTypes () const override;
  QMimeData * mimeData (QModelIndexList const&) const override;

  static int constexpr num_cols {SENTINAL};
  static auto constexpr mime_type = "application/wsjt.Frequencies";

  Bands const * bands_;
  FrequencyItems frequency_list_;
  Region region_filter_;
  Mode mode_filter_;
};

FrequencyList_v2::FrequencyList_v2 (Bands const * bands, QObject * parent)
  : QSortFilterProxyModel {parent}
  , m_ {bands, parent}
{
  setSourceModel (&*m_);
  setSortRole (SortRole);
}

FrequencyList_v2::~FrequencyList_v2 ()
{
}

auto FrequencyList_v2::frequency_list (FrequencyItems frequency_list) -> FrequencyItems
{
  return m_->frequency_list (frequency_list);
}

auto FrequencyList_v2::frequency_list () const -> FrequencyItems const&
{
  return m_->frequency_list_;
}

auto FrequencyList_v2::frequency_list (QModelIndexList const& model_index_list) const -> FrequencyItems
{
  FrequencyItems list;
  Q_FOREACH (auto const& index, model_index_list)
    {
      list << m_->frequency_list_[mapToSource (index).row ()];
    }
  return list;
}

void FrequencyList_v2::frequency_list_merge (FrequencyItems const& items)
{
  m_->add (items);
}

int FrequencyList_v2::best_working_frequency (Frequency f) const
{
  int result {-1};
  auto const& target_band = m_->bands_->find (f);
  if (!target_band.isEmpty ())
    {
      Radio::FrequencyDelta delta {std::numeric_limits<Radio::FrequencyDelta>::max ()};
      // find a frequency in the same band that is allowed
      for (int row = 0; row < rowCount (); ++row)
        {
          auto const& source_row = mapToSource (index (row, 0)).row ();
          auto const& candidate_frequency = m_->frequency_list_[source_row].frequency_;
          auto const& band = m_->bands_->find (candidate_frequency);
          if (band == target_band)
            {
              // take closest band match
              Radio::FrequencyDelta new_delta = f - candidate_frequency;
              if (std::abs (new_delta) < std::abs (delta))
                {
                  delta = new_delta;
                  result = row;
                }
            }
        }
    }
  return result;
}

int FrequencyList_v2::best_working_frequency (QString const& target_band) const
{
  int result {-1};
  if (!target_band.isEmpty ())
    {
      // find a frequency in the same band that is allowed
      for (int row = 0; row < rowCount (); ++row)
        {
          auto const& source_row = mapToSource (index (row, 0)).row ();
          auto const& band = m_->bands_->find (m_->frequency_list_[source_row].frequency_);
          if (band == target_band)
            {
              return row;
            }
        }
    }
  return result;
}

void FrequencyList_v2::reset_to_defaults ()
{
  m_->frequency_list (default_frequency_list);
}

QModelIndex FrequencyList_v2::add (Item f)
{
  return mapFromSource (m_->add (f));
}

bool FrequencyList_v2::remove (Item f)
{
  auto row = m_->frequency_list_.indexOf (f);

  if (0 > row)
    {
      return false;
    }

  return m_->removeRow (row);
}

namespace
{
  bool row_is_higher (QModelIndex const& lhs, QModelIndex const& rhs)
  {
    return lhs.row () > rhs.row ();
  }
}

bool FrequencyList_v2::removeDisjointRows (QModelIndexList rows)
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

void FrequencyList_v2::filter (Region region, Mode mode)
{
  m_->region_filter_ = region;
  m_->mode_filter_ = mode;
  invalidateFilter ();
}

bool FrequencyList_v2::filterAcceptsRow (int source_row, QModelIndex const& /* parent */) const
{
  bool result {true};
  auto const& item = m_->frequency_list_[source_row];
  if (m_->region_filter_ != IARURegions::ALL)
    {
      result = IARURegions::ALL == item.region_ || m_->region_filter_ == item.region_;
    }
  if (result && m_->mode_filter_ != Modes::ALL)
    {
      result = Modes::ALL == item.mode_ || m_->mode_filter_ == item.mode_;
    }
  return result;
}


auto FrequencyList_v2::impl::frequency_list (FrequencyItems frequency_list) -> FrequencyItems
{
  beginResetModel ();
  std::swap (frequency_list_, frequency_list);
  endResetModel ();
  return frequency_list;
}

QModelIndex FrequencyList_v2::impl::add (Item f)
{
  // Any Frequency that isn't in the list may be added
  if (!frequency_list_.contains (f))
    {
      auto row = frequency_list_.size ();

      beginInsertRows (QModelIndex {}, row, row);
      frequency_list_.append (f);
      endInsertRows ();

      return index (row, 0);
    }
  return QModelIndex {};
}

void FrequencyList_v2::impl::add (FrequencyItems items)
{
  // Any Frequency that isn't in the list may be added
  for (auto p = items.begin (); p != items.end ();)
    {
      if (frequency_list_.contains (*p))
        {
          p = items.erase (p);
        }
      else
        {
          ++p;
        }
    }

  if (items.size ())
    {
      auto row = frequency_list_.size ();

      beginInsertRows (QModelIndex {}, row, row + items.size () - 1);
      frequency_list_.append (items);
      endInsertRows ();
    }
}

int FrequencyList_v2::impl::rowCount (QModelIndex const& parent) const
{
  return parent.isValid () ? 0 : frequency_list_.size ();
}

int FrequencyList_v2::impl::columnCount (QModelIndex const& parent) const
{
  return parent.isValid () ? 0 : num_cols;
}

Qt::ItemFlags FrequencyList_v2::impl::flags (QModelIndex const& index) const
{
  auto result = QAbstractTableModel::flags (index) | Qt::ItemIsDropEnabled;
  auto row = index.row ();
  auto column = index.column ();
  if (index.isValid ()
      && row < frequency_list_.size ()
      && column < num_cols)
    {
      if (frequency_mhz_column != column)
        {
          result |= Qt::ItemIsEditable | Qt::ItemIsDragEnabled;
        }
    }
  return result;
}

QVariant FrequencyList_v2::impl::data (QModelIndex const& index, int role) const
{
  QVariant item;

  auto const& row = index.row ();
  auto const& column = index.column ();

  if (index.isValid ()
      && row < frequency_list_.size ()
      && column < num_cols)
    {
      auto const& frequency_item = frequency_list_.at (row);
      switch (column)
        {
        case region_column:
          switch (role)
            {
            case SortRole:
            case Qt::DisplayRole:
            case Qt::EditRole:
            case Qt::AccessibleTextRole:
              item = IARURegions::name (frequency_item.region_);
              break;

            case Qt::ToolTipRole:
            case Qt::AccessibleDescriptionRole:
              item = tr ("IARU Region");
              break;

            case Qt::TextAlignmentRole:
              item = Qt::AlignCenter;
              break;
            }
          break;

        case mode_column:
          switch (role)
            {
            case SortRole:
            case Qt::DisplayRole:
            case Qt::EditRole:
            case Qt::AccessibleTextRole:
              item = Modes::name (frequency_item.mode_);
              break;

            case Qt::ToolTipRole:
            case Qt::AccessibleDescriptionRole:
              item = tr ("Mode");
              break;

            case Qt::TextAlignmentRole:
              item = Qt::AlignCenter;
              break;
            }
          break;

        case frequency_column:
          switch (role)
            {
            case SortRole:
            case Qt::EditRole:
            case Qt::AccessibleTextRole:
              item = frequency_item.frequency_;
              break;

            case Qt::DisplayRole:
              {
                auto const& band = bands_->find (frequency_item.frequency_);
                item = Radio::pretty_frequency_MHz_string (frequency_item.frequency_)
                  + " MHz (" + (band.isEmpty () ? "OOB" : band) + ')';
              }
              break;

            case Qt::ToolTipRole:
            case Qt::AccessibleDescriptionRole:
              item = tr ("Frequency");
              break;

            case Qt::TextAlignmentRole:
              item = static_cast<Qt::Alignment::Int>(Qt::AlignRight | Qt::AlignVCenter);
              break;
            }
          break;

        case frequency_mhz_column:
          switch (role)
            {
            case Qt::EditRole:
            case Qt::AccessibleTextRole:
              item = Radio::frequency_MHz_string (frequency_item.frequency_);
              break;

            case Qt::DisplayRole:
              {
                auto const& band = bands_->find (frequency_item.frequency_);
                item = Radio::pretty_frequency_MHz_string (frequency_item.frequency_)
                  + " MHz (" + (band.isEmpty () ? "OOB" : band) + ')';
              }
              break;

            case Qt::ToolTipRole:
            case Qt::AccessibleDescriptionRole:
              item = tr ("Frequency (MHz)");
              break;

            case Qt::TextAlignmentRole:
              item = static_cast<Qt::Alignment::Int>(Qt::AlignRight | Qt::AlignVCenter);
              break;
            }
          break;
        }
    }
  return item;
}

bool FrequencyList_v2::impl::setData (QModelIndex const& model_index, QVariant const& value, int role)
{
  bool changed {false};

  auto const& row = model_index.row ();
  if (model_index.isValid ()
      && Qt::EditRole == role
      && row < frequency_list_.size ())
    {
      QVector<int> roles;
      roles << role;

      auto& item = frequency_list_[row];
      switch (model_index.column ())
        {
        case region_column:
          {
            auto region = IARURegions::value (value.toString ());
            if (region != item.region_)
              {
                item.region_ = region;
                Q_EMIT dataChanged (model_index, model_index, roles);
                changed = true;
              }
            }
          break;

        case mode_column:
          {
            auto mode = Modes::value (value.toString ());
            if (mode != item.mode_)
              {
                item.mode_ = mode;
                Q_EMIT dataChanged (model_index, model_index, roles);
                changed = true;
              }
          }
          break;

        case frequency_column:
          if (value.canConvert<Frequency> ())
            {
              Radio::Frequency frequency {qvariant_cast <Radio::Frequency> (value)};
              if (frequency != item.frequency_)
                {
                  item.frequency_ = frequency;
                  // mark derived column (1) changed as well
                  Q_EMIT dataChanged (index (model_index.row (), 1), model_index, roles);
                  changed = true;
                }
            }
          break;
        }
    }

  return changed;
}

QVariant FrequencyList_v2::impl::headerData (int section, Qt::Orientation orientation, int role) const
{
  QVariant header;
  if (Qt::DisplayRole == role
      && Qt::Horizontal == orientation
      && section < num_cols)
    {
      switch (section)
        {
        case region_column: header = tr ("IARU Region"); break;
        case mode_column: header = tr ("Mode"); break;
        case frequency_column: header = tr ("Frequency"); break;
        case frequency_mhz_column: header = tr ("Frequency (MHz)"); break;
        }
    }
  else
    {
      header = QAbstractTableModel::headerData (section, orientation, role);
    }
  return header;
}

bool FrequencyList_v2::impl::removeRows (int row, int count, QModelIndex const& parent)
{
  if (0 < count && (row + count) <= rowCount (parent))
    {
      beginRemoveRows (parent, row, row + count - 1);
      for (auto r = 0; r < count; ++r)
        {
          frequency_list_.removeAt (row);
        }
      endRemoveRows ();
      return true;
    }
  return false;
}

bool FrequencyList_v2::impl::insertRows (int row, int count, QModelIndex const& parent)
{
  if (0 < count)
    {
      beginInsertRows (parent, row, row + count - 1);
      for (auto r = 0; r < count; ++r)
        {
          frequency_list_.insert (row, Item {0, Mode::ALL, IARURegions::ALL});
        }
      endInsertRows ();
      return true;
    }
  return false;
}

QStringList FrequencyList_v2::impl::mimeTypes () const
{
  QStringList types;
  types << mime_type;
  return types;
}

QMimeData * FrequencyList_v2::impl::mimeData (QModelIndexList const& items) const
{
  QMimeData * mime_data = new QMimeData {};
  QByteArray encoded_data;
  QDataStream stream {&encoded_data, QIODevice::WriteOnly};

  Q_FOREACH (auto const& item, items)
    {
      if (item.isValid () && frequency_column == item.column ())
        {
          stream << frequency_list_.at (item.row ());
        }
    }

  mime_data->setData (mime_type, encoded_data);
  return mime_data;
}

auto FrequencyList_v2::const_iterator::operator * () const -> Item const&
{
  return parent_->frequency_list ().at(parent_->mapToSource (parent_->index (row_, 0)).row ());
}

auto FrequencyList_v2::const_iterator::operator -> () const -> Item const *
{
  return &parent_->frequency_list ().at(parent_->mapToSource (parent_->index (row_, 0)).row ());
}

bool FrequencyList_v2::const_iterator::operator != (const_iterator const& rhs) const
{
  return parent_ != rhs.parent_ || row_ != rhs.row_;
}

bool FrequencyList_v2::const_iterator::operator == (const_iterator const& rhs) const
{
  return parent_ == rhs.parent_ && row_ == rhs.row_;
}

auto FrequencyList_v2::const_iterator::operator ++ () -> const_iterator&
{
  ++row_;
  return *this;
}

auto FrequencyList_v2::begin () const -> const_iterator
{
  return const_iterator (this, 0);
}

auto FrequencyList_v2::end () const -> const_iterator
{
  return const_iterator (this, rowCount ());
}

auto FrequencyList_v2::find (Frequency f) const -> const_iterator
{
  int row {0};
  for (; row < rowCount (); ++row)
    {
      if (m_->frequency_list_[mapToSource (index (row, 0)).row ()].frequency_ == f)
        {
          break;
        }
    }
  return const_iterator (this, row);
}

auto FrequencyList_v2::filtered_bands () const -> BandSet
{
  BandSet result;
  for (auto const& item : *this)
    {
      result << m_->bands_->find (item.frequency_);
    }
  return result;
}

auto FrequencyList_v2::all_bands (Region region, Mode mode) const -> BandSet
{
  BandSet result;
  for (auto const& item : m_->frequency_list_)
    {
      if (region == IARURegions::ALL || item.region_ == region
          || mode == Modes::ALL || item.mode_ == mode)
        {
          result << m_->bands_->find (item.frequency_);
        }
    }
  return result;
}

//
// Obsolete version of FrequencyList no longer used but needed to
// allow loading and saving of old settings contents without damage
//
QDataStream& operator << (QDataStream& os, FrequencyList::Item const& item)
{
  return os << item.frequency_
            << item.mode_;
}

QDataStream& operator >> (QDataStream& is, FrequencyList::Item& item)
{
  return is >> item.frequency_
            >> item.mode_;
}
