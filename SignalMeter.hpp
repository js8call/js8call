#ifndef SIGNALMETER_HPP__
#define SIGNALMETER_HPP__

#include <QWidget>

class QLabel;

class SignalMeter final : public QWidget
{
  Q_OBJECT

public:

  // Constructor

  explicit SignalMeter(QWidget * parent = nullptr);

  // Slots

  Q_SLOT void setValue(float value,
                       float valueMax);

private:

  // Forward declarations

  class Scale;
  class Meter;

  // Data members

  Scale  * m_scale;
  Meter  * m_meter;
  QLabel * m_value;
};

#endif // SIGNALMETER_HPP__
