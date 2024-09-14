#ifndef LIVE_FREQUENCY_VALIDATOR_HPP__
#define LIVE_FREQUENCY_VALIDATOR_HPP__

#include <QObject>
#include <QRegularExpressionValidator>

#include "Radio.hpp"

class Bands;
class FrequencyList_v2;
class QComboBox;
class QWidget;

//
// Class LiveFrequencyValidator
//
//	QLineEdit validator that controls input to an editable
//	QComboBox where the user can enter a valid band or a valid
//	frequency in megahetz.
//
// Collabrations
//
//	Implements the QRegExpValidator interface. Validates input
//	from the supplied QComboBox as either a valid frequency in
//	megahertz or a valid band as defined by the supplied column of
//	the supplied QAbstractItemModel.
//
class LiveFrequencyValidator final
  : public QRegularExpressionValidator
{
  Q_OBJECT;

public:
  using Frequency = Radio::Frequency;

  LiveFrequencyValidator (QComboBox * combo_box // associated combo box
                          , Bands const * bands // bands model
                          , FrequencyList_v2 const * frequencies // working frequencies model
                          , Frequency const * nominal_frequency
                          , QWidget * parent = nullptr);

  State validate (QString& input, int& pos) const override;
  void fixup (QString& input) const override;

  Q_SIGNAL void valid (Frequency) const;

private:
  Bands const * bands_;
  FrequencyList_v2 const * frequencies_;
  Frequency const * nominal_frequency_;
  QComboBox * combo_box_;
};

#endif
