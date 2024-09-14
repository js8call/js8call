#ifndef MAIDENHEAD_LOCATOR_VALIDATOR_HPP__
#define MAIDENHEAD_LOCATOR_VALIDATOR_HPP__

#include <QValidator>
#include <QRegularExpression>

//
// MaidenheadLocatorValidator - QValidator implementation for grid locators
//
class MaidenheadLocatorValidator final
  : public QValidator
{
public:
  enum class Length {field = 2, square = 4, subsquare = 6, extended = 8};
  MaidenheadLocatorValidator (QObject * parent = nullptr
                              , Length length = Length::subsquare
                              , Length required = Length::square);

  // QValidator implementation
  State validate (QString& input, int& pos) const override;

private:
  QRegularExpression re_;
};

#endif
