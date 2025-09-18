#ifndef FREQUENCY_LINE_EDIT_HPP_
#define FREQUENCY_LINE_EDIT_HPP_

#include <QLineEdit>

#include "Radio.hpp"

class QWidget;

//
// MHz frequency line edits with validation
//
class FrequencyLineEdit final
  : public QLineEdit
{
  Q_OBJECT;
  Q_PROPERTY (Frequency frequency READ frequency WRITE frequency USER true);

public:
  using Frequency = Radio::Frequency;

  explicit FrequencyLineEdit (QWidget * parent = nullptr);

  // Property frequency implementation
  Frequency frequency () const;
  void frequency (Frequency);
};

class FrequencyDeltaLineEdit final
  : public QLineEdit
{
  Q_OBJECT;
  Q_PROPERTY (FrequencyDelta frequency_delta READ frequency_delta WRITE frequency_delta USER true);

public:
  using FrequencyDelta = Radio::FrequencyDelta;

  explicit FrequencyDeltaLineEdit (QWidget * parent = nullptr);

  // Property frequency_delta implementation
  FrequencyDelta frequency_delta () const;
  void frequency_delta (FrequencyDelta);
};

#endif
