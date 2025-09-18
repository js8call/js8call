#ifndef SETTINGS_GROUP_HPP_
#define SETTINGS_GROUP_HPP_

#include <QSettings>
#include <QString>

//
// Class SettingsGroup
//
//	Simple RAII type class to apply a QSettings group witin a
//	scope.
//
class SettingsGroup
{
public:
  SettingsGroup (QSettings * settings, QString const& group)
    : settings_ {settings}
  {
    settings_->beginGroup (group);
  }

  SettingsGroup (SettingsGroup const&) = delete;
  SettingsGroup& operator = (SettingsGroup const&) = delete;

  ~SettingsGroup ()
  {
    settings_->endGroup ();
  }

private:
  QSettings * settings_;
};

#endif
