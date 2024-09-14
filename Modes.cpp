#include "Modes.hpp"

#include <algorithm>

#include <QString>
#include <QVariant>
#include <QModelIndex>

#include "moc_Modes.cpp"

namespace
{
  // human readable strings for each Mode enumeration value
  char const * const mode_names[] =
  {
    "All",
    "JS8",
  };
  std::size_t constexpr mode_names_size = sizeof (mode_names) / sizeof (mode_names[0]);
}

Modes::Modes (QObject * parent)
  : QAbstractListModel {parent}
{
  static_assert (mode_names_size == MODES_END_SENTINAL_AND_COUNT,
                 "mode_names array must match Mode enumeration");
}

char const * Modes::name (Mode m)
{
  return mode_names[static_cast<int> (m)];
}

auto Modes::value (QString const& s) -> Mode
{
  auto end = mode_names + mode_names_size;
  auto p = std::find_if (mode_names, end
                         , [&s] (char const * const name) {
                           return name == s;
                         });
  return p != end ? static_cast<Mode> (p - mode_names) : ALL;
}

QVariant Modes::data (QModelIndex const& index, int role) const
{
  QVariant item;

  if (index.isValid ())
    {
      auto const& row = index.row ();
      switch (role)
        {
        case Qt::ToolTipRole:
        case Qt::AccessibleDescriptionRole:
          item = tr ("Mode");
          break;

        case Qt::EditRole:
          item = static_cast<Mode> (row);
          break;

        case Qt::DisplayRole:
        case Qt::AccessibleTextRole:
          item = mode_names[row];
          break;

        case Qt::TextAlignmentRole:
          item = Qt::AlignCenter;
          break;
        }
    }

  return item;
}

QVariant Modes::headerData (int section, Qt::Orientation orientation, int role) const
{
  QVariant result;

  if (Qt::DisplayRole == role && Qt::Horizontal == orientation)
    {
      result = tr ("Mode");
    }
  else
    {
      result = QAbstractListModel::headerData (section, orientation, role);
    }

  return result;
}

#if !defined (QT_NO_DEBUG_STREAM)
ENUM_QDEBUG_OPS_IMPL (Modes, Mode);
#endif

ENUM_QDATASTREAM_OPS_IMPL (Modes, Mode);
ENUM_CONVERSION_OPS_IMPL (Modes, Mode);
