#include "LiveFrequencyValidator.hpp"

#include <QLocale>
#include <QString>
#include <QComboBox>
#include <QLineEdit>

#include "Bands.hpp"
#include "FrequencyList.hpp"

#include "moc_LiveFrequencyValidator.cpp"

LiveFrequencyValidator::LiveFrequencyValidator (QComboBox * combo_box
                                                , Bands const * bands
                                                , FrequencyList_v2 const * frequencies
                                                , Frequency const * nominal_frequency
                                                , QWidget * parent)
  : QRegularExpressionValidator {
      QRegularExpression {       // frequency in MHz or band
        bands->data (QModelIndex {}).toString () // out of band string
          + QString {R"(|((\d{0,6}(\)"}    // or up to 6 digits
          + QLocale {}.decimalPoint () // (followed by decimal separator
          + R"(\d{0,2})?)([Mm]{1,2}|([Cc][Mm])))|(\d{0,6}(\)" // followed by up to 2 digits and either 'm' or 'cm' or 'mm' (case insensitive))
          + QLocale {}.decimalPoint () // or a decimal separator
          + R"(\d{0,6})?)|(\d{0,3}(\)"        //  followed by up to 6
                                              //  digits or a decimal number
          + QLocale {}.decimalPoint () // or a decimal separator
          + R"(\d{0,6})?[Kk]))"        // followed by a 'k' or 'K'
      }
      , parent
    }
  , bands_ {bands}
  , frequencies_ {frequencies}
  , nominal_frequency_ {nominal_frequency}
  , combo_box_ {combo_box}
{
}

auto LiveFrequencyValidator::validate (QString& input, int& pos) const -> State
{
  auto state = QRegularExpressionValidator::validate (input, pos);
  // by never being Acceptable we force fixup calls on ENTER or
  // losing focus
  return Acceptable == state ? Intermediate : state;
}

void LiveFrequencyValidator::fixup (QString& input) const
{
  QRegularExpressionValidator::fixup (input);
  if (!bands_->oob ().startsWith (input))
    {
      if (input.contains ('m', Qt::CaseInsensitive))
        {
          input = input.toLower ();

          QVector<QVariant> frequencies;
          for (auto const& item : frequencies_->frequency_list ())
            {
              if (bands_->find (item.frequency_) == input)
                {
                  frequencies << item.frequency_;
                }
            }
          if (!frequencies.isEmpty ())
            {
              Q_EMIT valid (frequencies.first ().value<Frequency> ());
            }
          else
            {
              input = QString {};
            }
        }
      else if (input.contains (QChar {'k'}, Qt::CaseInsensitive))
        {
          // kHz in current MHz input
          auto f = Radio::frequency (input.remove (QChar {'k'}, Qt::CaseInsensitive), 3);
          f += *nominal_frequency_ / 1000000u * 1000000u;
          input = bands_->find (f);
          Q_EMIT valid (f);
        }
      else
        {
          // frequency input
          auto f = Radio::frequency (input, 6);
          input = bands_->find (f);
          Q_EMIT valid (f);
        }

      if (bands_->oob () == input)
        {
          combo_box_->lineEdit ()->setStyleSheet ("QLineEdit {color: yellow; background-color : red;}");
        }
      else
        {
          combo_box_->lineEdit ()->setStyleSheet ({});
        }
      combo_box_->setCurrentText (input);
    }
}
