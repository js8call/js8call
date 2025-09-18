#ifndef LAZY_FILL_COMBO_BOX_HPP__
#define LAZY_FILL_COMBO_BOX_HPP__

#include <QComboBox>

class QWidget;

//
// Class LazyFillComboBox
//
// QComboBox derivative that signals show and hide of the pop up list.
//
class LazyFillComboBox
  : public QComboBox
{
  Q_OBJECT

public:
  Q_SIGNAL void about_to_show_popup ();
  Q_SIGNAL void popup_hidden ();

  explicit LazyFillComboBox (QWidget * parent = nullptr)
    : QComboBox {parent}
  {
  }

#if QT_VERSION >= QT_VERSION_CHECK (5, 12, 0)
  void showPopup () override
  {
    Q_EMIT about_to_show_popup ();
    QComboBox::showPopup ();
  }

  void hidePopup () override
  {
    QComboBox::hidePopup ();
    Q_EMIT popup_hidden ();
  }
#else
  void mousePressEvent (QMouseEvent * e) override
  {
    Q_EMIT about_to_show_popup ();
    QComboBox::mousePressEvent (e);
  }

  void mouseReleaseEvent (QMouseEvent * e) override
  {
    QComboBox::mouseReleaseEvent (e);
    Q_EMIT popup_hidden ();
  }
#endif
};

#endif
