#include "qt_helpers.hpp"

#include <QString>
#include <QFont>
#include <QWidget>
#include <QStyle>
#include <QVariant>

QString font_as_stylesheet (QFont const& font)
{
  QString font_weight;
  switch (font.weight ())
    {
    case QFont::Thin: font_weight = "thin"; break;
    case QFont::ExtraLight: font_weight = "extralight"; break;
    case QFont::Light: font_weight = "light"; break;
    case QFont::Normal: font_weight = "normal"; break;
    case QFont::Medium: font_weight = "medium"; break;
    case QFont::DemiBold: font_weight = "demibold"; break;
    case QFont::Bold: font_weight = "bold"; break;
    case QFont::ExtraBold: font_weight = "extrabold"; break;
    case QFont::Black: font_weight = "black"; break;
    }
  return QString {
      " font-family: %1;\n"
      " font-size: %2pt;\n"
      " font-style: %3;\n"
      " font-weight: %4;\n"}
  .arg (font.family ())
     .arg (font.pointSize ())
     .arg (font.styleName ())
     .arg (font_weight);
}

void update_dynamic_property (QWidget * widget, char const * property, QVariant const& value)
{
  widget->setProperty (property, value);
  widget->style ()->unpolish (widget);
  widget->style ()->polish (widget);
  widget->update ();
}
