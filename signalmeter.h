// -*- Mode: C++ -*-
#ifndef SIGNALMETER_H
#define SIGNALMETER_H

#include <QFrame>

class QLabel;

class SignalMeter final
  : public QFrame
{
  Q_OBJECT

public:
  explicit SignalMeter (QWidget * parent = nullptr);

public slots:
  void setValue (float value, float valueMax);

private:

  class Scale;
  class Meter;

  Scale  * m_scale;
  Meter  * m_meter;
  QLabel * m_value;
};

#endif // SIGNALMETER_H
