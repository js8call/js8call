#ifndef MULTISETTINGS_HPP__
#define MULTISETTINGS_HPP__

#include <QObject>
#include <QVariant>
#include <QString>

#include "pimpl_h.hpp"

class QSettings;
class QMainWindow;
class QMenu;

//
// MultiSettings - Manage multiple configuration names
//
// Responsibilities:
//
//  MultiSettings allows a  Qt application to be  run with alternative
//  settings as  stored in a QSettings  INI style file. As  far as the
//  application is  concerned it uses the  QSettings instance returned
//  by the MultiSettings::settings() method as  if it were the one and
//  only  QSettings object.   The alternative  settings are  stored as
//  QSettings groups which  are children of a root  level group called
//  MultiSettings. The  current settings are themselves  stored at the
//  root so the QSettings group  name MultiSettings is reserved.  Also
//  at the  root level a key  called CurrentMultiSettingsConfiguration
//  is reserved to store the current configuration name.
//
//
// Example Usage:
//
//  #include <QApplication>
//  #include "MultiSettings.hpp"
//  #include "MyMainWindow.hpp"
//
//  int main (int argc, char * argv[]) {
//    QApplication a {argc, argv};
//    MultiSettings multi_settings;
//    int result;
//    do {
//      MyMainWindow main_window {&multi_settings};
//      main_window.show ();
//      result = a.exec ();
//    } while (!result && !multi_settings.exit ());
//    return result;
//  }
//
//  In  the main  window call  MultiSettings::create_menu_actions() to
//  populate an existing QMenu widget with the configuration switching
//  and maintenance actions.  This would normally be done  in the main
//  window class constructor:
//
//  MyMainWindow::MyMainWindow (MultiSettings * multi_settings) {
//    QSettings * settings {multi_settings->settings ()};
//    // ...
//    multi_settings->create_menu_actions (this, ui->configurations_menu);
//    // ...
//  }
//

class MultiSettings
  : public QObject
{
  Q_OBJECT

public:
  // config_name will be  selected if it is  an existing configuration
  // name otherwise  the last used  configuration will be  selected or
  // the default configuration if none exist
  explicit MultiSettings (QString const& config_name = QString {});

  MultiSettings (MultiSettings const&) = delete;
  MultiSettings& operator = (MultiSettings const&) = delete;
  ~MultiSettings ();

  // Add multiple configurations navigation and maintenance actions to
  // a provided  menu. The provided  main window object  instance will
  // have its close() function called when a "Switch To" configuration
  // action is triggered.
  void create_menu_actions (QMainWindow *, QMenu *);

  // switch to this configuration if it exists
  Q_SLOT void select_configuration (QString const& name);
  QString configuration_name () const;

  // Access to the QSettings object instance.
  QSettings * settings ();

  // Access to values in a common section
  QVariant common_value (QString const& key, QVariant const& default_value = QVariant {}) const;
  void set_common_value (QString const& key, QVariant const& value);
  void remove_common_value (QString const& key);

  // Call this to  determine if the application is  terminating, if it
  // returns  false  then  the   application  main  window  should  be
  // recreated,  shown  and  the application  exec()  function  called
  // again.
  bool exit ();

  // emitted when the name of the current configuration changes
  Q_SIGNAL void configurationNameChanged (QString name) const;

private:
  class impl;
  pimpl<impl> m_;
};

#endif
