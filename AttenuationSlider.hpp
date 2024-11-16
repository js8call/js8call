#ifndef ATTENUATIONSLIDER_HPP__
#define ATTENUATIONSLIDER_HPP__

#include <QSlider>

class AttenuationSlider final : public QSlider
{
public:

  explicit AttenuationSlider(QWidget * parent = nullptr)
  : QSlider{parent}
  {}

protected:

  void paintEvent(QPaintEvent *) override;

private:

  int yValue(int) const;
};

#endif // ATTENUATIONSLIDER_HPP__
