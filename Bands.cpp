#include "Bands.hpp"

#include <algorithm>

#include <QString>
#include <QVariant>

namespace
{
  // Table of ADIF band definitions as defined in the ADIF
  // specification as at ADIF v3.0.6
  struct ADIFBand
  {
    char const * const name_;
    Radio::Frequency lower_bound_;
    Radio::Frequency upper_bound_;
  } constexpr ADIF_bands[] = {
    {"2190m",	136000u,  		137000u},
    {"630m",  472000u,  		479000u},
    {"560m",  501000u,  		504000u},
    {"160m",  1800000u,   	2000000u},
    {"80m",   3500000u,   	4000000u},
    {"60m",   5060000u,   	5450000u},
    {"40m",   7000000u,   	7300000u},
    {"30m",   10000000u,  	10150000u},
    {"20m",   14000000u,  	14350000u},
    {"17m",   18068000u,  	18168000u},
    {"15m",   21000000u,  	21450000u},
    {"12m",   24890000u,  	24990000u},
    {"10m",   28000000u,  	29700000u},
    {"6m",  	50000000u,  	54000000u},
    {"4m",  	70000000u,  	71000000u},
    {"2m",  	144000000u,   148000000u},
    {"1.25m", 222000000u,   225000000u},
    {"70cm",  420000000u,   450000000u},
    {"33cm",  902000000u,   928000000u},
    {"23cm",  1240000000u,  1300000000u},
    {"13cm",  2300000000u,  2450000000u},
    {"9cm",   3300000000u,  3500000000u},
    {"6cm",   5650000000u,  5925000000u},
    {"3cm",   10000000000u, 10500000000u},
    {"1.25cm",24000000000u, 24250000000u},
    {"6mm",   47000000000u, 47200000000u},
    {"4mm",   75500000000u, 81000000000u},
    {"2.5mm", 119980000000u,120020000000u},
    {"2mm",   142000000000u,149000000000u},
    {"1mm",   241000000000u,250000000000u},
  };

  QString const oob_name {QObject::tr ("OOB")};

  int constexpr table_rows ()
  {
    return sizeof (ADIF_bands) / sizeof (ADIF_bands[0]);
  }
}

Bands::Bands (QObject * parent)
  : QAbstractTableModel {parent}
{
}

QString Bands::find (Frequency f) const
{
  QString result;
  auto const& end_iter = ADIF_bands + table_rows ();
  auto const& row_iter = std::find_if (ADIF_bands, end_iter, [f] (ADIFBand const& band) {
      return band.lower_bound_ <= f && f <= band.upper_bound_;
    });
  if (row_iter != end_iter)
    {
      result = row_iter->name_;
    }
  return result;
}

int Bands::find (QString const& band) const
{
  int result {-1};
  for (auto i = 0u; i < table_rows (); ++i)
    {
      if (band == ADIF_bands[i].name_)
        {
          result = i;
        }
    }
  return result;
}

bool Bands::findFreq(QString const& band, Radio::Frequency *pFreqLower, Radio::Frequency *pFreqHigher) const {
    int row = find(band);
    if(row == -1){
        return false;
    }

    if(pFreqLower) *pFreqLower = ADIF_bands[row].lower_bound_;
    if(pFreqHigher) *pFreqHigher = ADIF_bands[row].upper_bound_;

    return true;
}


QString const& Bands::oob ()
{
  return oob_name;
}

int Bands::rowCount (QModelIndex const& parent) const
{
  return parent.isValid () ? 0 : table_rows ();
}

int Bands::columnCount (QModelIndex const& parent) const
{
  return parent.isValid () ? 0 : 3;
}

Qt::ItemFlags Bands::flags (QModelIndex const& index) const
{
  return QAbstractTableModel::flags (index) | Qt::ItemIsDropEnabled;
}

QVariant Bands::data (QModelIndex const& index, int role) const
{
  QVariant item;

  if (!index.isValid ())
    {
      // Hijack root for OOB string.
      if (Qt::DisplayRole == role)
        {
          item = oob_name;
        }
    }
  else
    {
      auto row = index.row ();
      auto column = index.column ();

      if (row < table_rows ())
        {
          switch (role)
            {
            case Qt::ToolTipRole:
            case Qt::AccessibleDescriptionRole:
              switch (column)
                {
                case 0: item = tr ("Band name"); break;
                case 1: item = tr ("Lower frequency limit"); break;
                case 2: item = tr ("Upper frequency limit"); break;
                }
              break;

            case SortRole:
            case Qt::DisplayRole:
            case Qt::EditRole:
              switch (column)
                {
                case 0:
                  if (SortRole == role)
                    {
                      // band name sorts by lower bound
                      item = ADIF_bands[row].lower_bound_;
                    }
                  else
                    {
                      item = ADIF_bands[row].name_;
                    }
                  break;

                case 1: item = ADIF_bands[row].lower_bound_; break;
                case 2: item = ADIF_bands[row].upper_bound_; break;
                }
              break;

            case Qt::AccessibleTextRole:
              switch (column)
                {
                case 0: item = ADIF_bands[row].name_; break;
                case 1: item = ADIF_bands[row].lower_bound_; break;
                case 2: item = ADIF_bands[row].upper_bound_; break;
                }
              break;

            case Qt::TextAlignmentRole:
              switch (column)
                {
                case 0:
                  item = Qt::AlignCenter;
                  break;

                case 1:
                case 2:
                  item = static_cast<Qt::Alignment::Int>(Qt::AlignRight | Qt::AlignVCenter);
                  break;
                }
              break;
            }
        }
    }
  return item;
}

QVariant Bands::headerData (int section, Qt::Orientation orientation, int role) const
{
  QVariant result;

  if (Qt::DisplayRole == role && Qt::Horizontal == orientation)
    {
      switch (section)
        {
        case 0: result = tr ("Band"); break;
        case 1: result = tr ("Lower Limit"); break;
        case 2: result = tr ("Upper Limit"); break;
        }
    }
  else
    {
      result = QAbstractTableModel::headerData (section, orientation, role);
    }

  return result;
}

QString Bands::const_iterator::operator * ()
{
  return ADIF_bands[row_].name_;
}

bool Bands::const_iterator::operator != (const_iterator const& rhs) const
{
  return row_ != rhs.row_;
}

auto Bands::const_iterator::operator ++ () -> const_iterator&
{
  ++row_;
  return *this;
}

auto Bands::begin () const -> Bands::const_iterator
{
  return const_iterator (0);
}

auto Bands::end () const -> Bands::const_iterator
{
  return const_iterator (table_rows ());
}
