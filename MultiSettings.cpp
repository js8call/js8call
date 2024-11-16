#include "MultiSettings.hpp"

#include <stdexcept>

#include <QObject>
#include <QSettings>
#include <QString>
#include <QStringList>
#include <QDir>
#include <QFont>
#include <QApplication>
#include <QStandardPaths>
#include <QMainWindow>
#include <QMenu>
#include <QAction>
#include <QActionGroup>
#include <QDialog>
#include <QLineEdit>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QComboBox>
#include <QLabel>
#include <QList>
#include <QMetaObject>

#include "SettingsGroup.hpp"
#include "qt_helpers.hpp"
#include "SettingsGroup.hpp"
#include "MessageBox.hpp"

#include "pimpl_impl.hpp"

namespace
{
  char const * default_string = QT_TRANSLATE_NOOP ("MultiSettings", "Default");
  char const * multi_settings_root_group = "MultiSettings";
  char const * multi_settings_current_group_key = "CurrentMultiSettingsConfiguration";
  char const * multi_settings_current_name_key = "CurrentName";
  char const * multi_settings_place_holder_key = "MultiSettingsPlaceHolder";

  QString unescape_ampersands (QString s)
  {
    return s.replace ("&&", "&");
  }

  // calculate a useable and unique settings file path
  QString settings_path ()
  {
    auto const& config_directory = QStandardPaths::writableLocation (QStandardPaths::ConfigLocation);
    QDir config_path {config_directory}; // will be "." if config_directory is empty
    if (!config_path.mkpath ("."))
      {
        throw std::runtime_error {"Cannot find a usable configuration path \"" + config_path.path ().toStdString () + '"'};
      }
    return config_path.absoluteFilePath (QApplication::applicationName () + ".ini");
  }

  //
  // Dialog to get a valid new configuration name
  //
  class NameDialog final
    : public QDialog
  {
    Q_OBJECT

  public:
    explicit NameDialog (QString const& current_name,
                         QStringList const& current_names,
                         QWidget * parent = nullptr)
      : QDialog {parent}
    {
      setWindowTitle (tr ("New Configuration Name"));

      auto form_layout = new QFormLayout ();
      form_layout->addRow (tr ("Old name:"), &old_name_label_);
      old_name_label_.setText (current_name);
      form_layout->addRow (tr ("&New name:"), &name_line_edit_);

      auto main_layout = new QVBoxLayout (this);
      main_layout->addLayout (form_layout);

      auto button_box = new QDialogButtonBox {QDialogButtonBox::Ok | QDialogButtonBox::Cancel};
      button_box->button (QDialogButtonBox::Ok)->setEnabled (false);
      main_layout->addWidget (button_box);

      auto name_validator = new QRegularExpressionValidator {QRegularExpression {R"([^/\\]+)"}, this};
      name_line_edit_.setValidator (name_validator);

      connect (&name_line_edit_, &QLineEdit::textChanged, [current_names, button_box] (QString const& name) {
          bool valid {!current_names.contains (name.trimmed ())};
          button_box->button (QDialogButtonBox::Ok)->setEnabled (valid);
        });
      connect (button_box, &QDialogButtonBox::accepted, this, &QDialog::accept);
      connect (button_box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    }

    QString new_name () const
    {
      return name_line_edit_.text ().trimmed ();
    }

  private:
    QLabel old_name_label_;
    QLineEdit name_line_edit_;
  };

  //
  // Dialog to get a valid new existing name
  //
  class ExistingNameDialog final
    : public QDialog
  {
    Q_OBJECT

  public:
    explicit ExistingNameDialog (QStringList const& current_names, QWidget * parent = nullptr)
      : QDialog {parent}
    {
      setWindowTitle (tr ("Configuration to Clone From"));

      name_combo_box_.addItems (current_names);

      auto form_layout = new QFormLayout ();
      form_layout->addRow (tr ("&Source Configuration Name:"), &name_combo_box_);

      auto main_layout = new QVBoxLayout (this);
      main_layout->addLayout (form_layout);

      auto button_box = new QDialogButtonBox {QDialogButtonBox::Ok | QDialogButtonBox::Cancel};
      main_layout->addWidget (button_box);

      connect (button_box, &QDialogButtonBox::accepted, this, &QDialog::accept);
      connect (button_box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    }

    QString name () const
    {
      return name_combo_box_.currentText ();
    }

  private:
    QComboBox name_combo_box_;
  };
}

class MultiSettings::impl final
  : public QObject
{
  Q_OBJECT

public:
  explicit impl (MultiSettings const * parent, QString const& config_name);
  bool reposition ();
  void create_menu_actions (QMainWindow * main_window, QMenu * menu);
  bool exit ();

  QSettings settings_;
  QString current_;

  // switch to this configuration
  void select_configuration (QString const& target_name);

private:
  using Dictionary = QVariantMap;

  // create a configuration maintenance sub menu
  QMenu * create_sub_menu (QMenu * parent,
                           QString const& menu_title,
                           QActionGroup * = nullptr);

  // extract all settings from the current QSettings group
  Dictionary get_settings () const;

  // write the settings values from the dictionary to the current group
  void load_from (Dictionary const&, bool add_placeholder = true);

  // clone this configuration
  void clone_configuration (QMenu *, QMenu const *);

  // update this configuration from another
  void clone_into_configuration (QMenu const *);

  // reset configuration to default values
  void reset_configuration (QMenu const *);

  // change configuration name
  void rename_configuration (QMenu *);

  // remove a configuration
  void delete_configuration (QMenu *);

  // action to take on restart
  enum class RepositionType {unchanged, replace, save_and_replace};
  void restart (RepositionType);

  MultiSettings const * parent_;  // required for emitting signals
  QMainWindow * main_window_;
  bool name_change_emit_pending_; // delayed until menu built

  QFont original_font_;

  RepositionType reposition_type_;
  Dictionary new_settings_;
  bool exit_flag_;              // false means loop around with new
                                // configuration
  QActionGroup * configurations_group_;
};

#include "MultiSettings.moc"
#include "moc_MultiSettings.cpp"

MultiSettings::MultiSettings (QString const& config_name)
  : m_ {this, config_name}
{
}

MultiSettings::~MultiSettings ()
{
}

QSettings * MultiSettings::settings ()
{
  return &m_->settings_;
}

QVariant MultiSettings::common_value (QString const& key, QVariant const& default_value) const
{
  QVariant value;
  QSettings * mutable_settings {const_cast<QSettings *> (&m_->settings_)};
  auto const& current_group = mutable_settings->group ();
  if (current_group.size ()) mutable_settings->endGroup ();
  {
    SettingsGroup alternatives {mutable_settings, multi_settings_root_group};
    value = mutable_settings->value (key, default_value);
  }
  if (current_group.size ()) mutable_settings->beginGroup (current_group);
  return value;
}

void MultiSettings::set_common_value (QString const& key, QVariant const& value)
{
  auto const& current_group = m_->settings_.group ();
  if (current_group.size ()) m_->settings_.endGroup ();
  {
    SettingsGroup alternatives {&m_->settings_, multi_settings_root_group};
    m_->settings_.setValue (key, value);
  }
  if (current_group.size ()) m_->settings_.beginGroup (current_group);
}

void MultiSettings::remove_common_value (QString const& key)
{
  if (!key.size ()) return;     // we don't allow global delete as it
                                // would break this classes data model
  auto const& current_group = m_->settings_.group ();
  if (current_group.size ()) m_->settings_.endGroup ();
  {
    SettingsGroup alternatives {&m_->settings_, multi_settings_root_group};
    m_->settings_.remove (key);
  }
  if (current_group.size ()) m_->settings_.beginGroup (current_group);
}

void MultiSettings::create_menu_actions (QMainWindow * main_window, QMenu * menu)
{
  m_->create_menu_actions (main_window, menu);
}

void MultiSettings::select_configuration (QString const& name)
{
  m_->select_configuration (name);
}

QString MultiSettings::configuration_name () const
{
  return m_->current_;
}

bool MultiSettings::exit ()
{
  return m_->exit ();
}

MultiSettings::impl::impl (MultiSettings const * parent, QString const& config_name)
  : settings_ {settings_path (), QSettings::IniFormat}
  , parent_ {parent}
  , main_window_ {nullptr}
  , name_change_emit_pending_ {true}
  , reposition_type_ {RepositionType::unchanged}
  , exit_flag_ {true}
  , configurations_group_ {new QActionGroup {this}}
{
  if (!settings_.isWritable ())
    {
      throw std::runtime_error {QString {"Cannot access \"%1\" for writing"}
        .arg (settings_.fileName ()).toStdString ()};
    }

  // deal with transient, now defunct, settings key
  if (settings_.contains (multi_settings_current_group_key))
    {
      current_ = settings_.value (multi_settings_current_group_key).toString ();
      settings_.remove (multi_settings_current_group_key);
      if (current_.size ())
        {
          {
            SettingsGroup alternatives {&settings_, multi_settings_root_group};
            {
              SettingsGroup source_group {&settings_, current_};
              new_settings_ = get_settings ();
            }
            settings_.setValue (multi_settings_current_name_key, tr (default_string));
          }
          reposition_type_ = RepositionType::save_and_replace;
          reposition ();
        }
      else
        {
          SettingsGroup alternatives {&settings_, multi_settings_root_group};
          settings_.setValue (multi_settings_current_name_key, tr (default_string));
        }
    }

  // bootstrap
  QStringList available_configurations;
  {
    SettingsGroup alternatives {&settings_, multi_settings_root_group};
    available_configurations = settings_.childGroups ();
    // use last selected configuration
    current_ = settings_.value (multi_settings_current_name_key).toString ();
    if (!current_.size ())
      {
        // no configurations so use default name
        current_ = tr (default_string);
        settings_.setValue (multi_settings_current_name_key, current_);
      }
  }

  if (config_name.size () && available_configurations.contains (config_name) && config_name != current_)
    {
      // switch to specified configuration
      {
        SettingsGroup alternatives {&settings_, multi_settings_root_group};
        // save the target settings
        SettingsGroup target_group {&settings_, config_name};
        new_settings_ = get_settings ();
      }
      current_ = config_name;
      reposition_type_ = RepositionType::save_and_replace;
      reposition ();
    }
  settings_.sync ();
}

// do actions that can only be done once all the windows are closed
bool MultiSettings::impl::reposition ()
{
  auto const& current_group = settings_.group ();
  if (current_group.size ()) settings_.endGroup ();
  switch (reposition_type_)
    {
    case RepositionType::save_and_replace:
      {
        // save the current settings with the other alternatives
        Dictionary saved_settings {get_settings ()};
        SettingsGroup alternatives {&settings_, multi_settings_root_group};
        // get the current configuration name
        auto const& previous_group_name = settings_.value (multi_settings_current_name_key, tr (default_string)).toString ();
        SettingsGroup save_group {&settings_, previous_group_name};
        load_from (saved_settings);
      }
      // fall through
    case RepositionType::replace:
      // and purge current settings
      for (auto const& key: settings_.allKeys ())
        {
          if (!key.contains (multi_settings_root_group))
            {
              settings_.remove (key);
            }
        }
      // insert the new settings
      load_from (new_settings_, false);
      if (!new_settings_.size ())
        {
          // if we are clearing the current settings then we must
          // reset the application font and the font in the
          // application style sheet, this is necessary since the
          // application instance is not recreated
          qApp->setFont (original_font_);
          qApp->setStyleSheet (qApp->styleSheet () + "* {" + font_as_stylesheet (original_font_) + '}');
        }
      // now we have set up the new current we can safely purge it
      // from the alternatives
      {
        SettingsGroup alternatives {&settings_, multi_settings_root_group};
        {
          SettingsGroup purge_group {&settings_, current_};
          settings_.remove (QString {});  // purge entire group
        }
        // switch to the specified configuration name
        settings_.setValue (multi_settings_current_name_key, current_);
      }
      settings_.sync ();
      // fall through
    case RepositionType::unchanged:
      new_settings_.clear ();
      break;
    }
  if (current_group.size ()) settings_.beginGroup (current_group);

  reposition_type_ = RepositionType::unchanged; // reset
  bool exit {exit_flag_};
  exit_flag_ = true;           // reset exit flag so normal exit works

  return exit;
}

// populate a pop up menu with the configurations sub-menus for
// maintenance including select, clone, clone from, delete, rename
// and, reset
void MultiSettings::impl::create_menu_actions (QMainWindow * main_window, QMenu * menu)
{
  main_window_ = main_window;
  auto const& current_group = settings_.group ();
  if (current_group.size ()) settings_.endGroup ();
  SettingsGroup alternatives {&settings_, multi_settings_root_group};
  // get the current configuration name
  auto const& current_configuration_name = settings_.value (multi_settings_current_name_key, tr (default_string)).toString ();
  // add the default configuration sub menu
  QMenu * default_menu = create_sub_menu (menu, current_configuration_name, configurations_group_);
  // and set as the current configuration
  default_menu->menuAction ()->setChecked (true);

  // get the existing alternatives
  auto const& available_configurations = settings_.childGroups ();

  // add all the other configurations
  for (auto const& configuration_name: available_configurations)
    {
      create_sub_menu (menu, configuration_name, configurations_group_);
    }

  if (current_group.size ()) settings_.beginGroup (current_group);

  if (name_change_emit_pending_)
    {
      Q_EMIT parent_->configurationNameChanged (unescape_ampersands (current_));
      name_change_emit_pending_ = false;
    }
}

// call this at the end of the main program loop to determine if the
// main window really wants to quit or to run again with a new configuration
bool MultiSettings::impl::exit ()
{
  // ensure that configuration name changed signal gets fired on restart
  name_change_emit_pending_ = true;

  // do any configuration swap required and return exit flag
  return reposition ();
}

QMenu * MultiSettings::impl::create_sub_menu (QMenu * parent_menu,
                                              QString const& menu_title,
                                              QActionGroup * action_group)
{
  auto sub_menu = parent_menu->addMenu (menu_title);
  if (action_group) action_group->addAction (sub_menu->menuAction ());
  sub_menu->menuAction ()->setCheckable (true);

  // populate sub-menu actions before showing
  connect (sub_menu, &QMenu::aboutToShow, [this, parent_menu, sub_menu] () {
      // depopulate before populating and showing because on Mac OS X
      // there is an issue with depopulating in QMenu::aboutToHide()
      // with connections being disconnected before they are actioned
      while (!sub_menu->actions ().isEmpty ())
        {
          sub_menu->removeAction (sub_menu->actions ().last ());
        }

      bool is_current {sub_menu->menuAction ()->text () == current_};
      if (!is_current)
        {
          auto select_action = new QAction {tr ("&Switch To"), this};
          sub_menu->addAction (select_action);
          connect (select_action, &QAction::triggered, [this, sub_menu] (bool) {
              select_configuration (sub_menu->title ());
            });
          sub_menu->addSeparator ();
        }

      auto clone_action = new QAction {tr ("&Clone"), this};
      sub_menu->addAction (clone_action);
      connect (clone_action, &QAction::triggered, [this, parent_menu, sub_menu] (bool) {
          clone_configuration (parent_menu, sub_menu);
        });

      auto const& current_group = settings_.group ();
      if (current_group.size ()) settings_.endGroup ();
      SettingsGroup alternatives {&settings_, multi_settings_root_group};
      if (settings_.childGroups ().size ())
        {
          auto clone_into_action = new QAction {tr ("Clone &Into ..."), this};
          sub_menu->addAction (clone_into_action);
          connect (clone_into_action, &QAction::triggered, [this, sub_menu] (bool) {
              clone_into_configuration (sub_menu);
            });
        }
      if (current_group.size ()) settings_.beginGroup (current_group);
      auto reset_action = new QAction {tr ("R&eset"), this};
      sub_menu->addAction (reset_action);
      connect (reset_action, &QAction::triggered, [this, sub_menu] (bool) {
          reset_configuration (sub_menu);
        });

      auto rename_action = new QAction {tr ("&Rename ..."), this};
      sub_menu->addAction (rename_action);
      connect (rename_action, &QAction::triggered, [this, sub_menu] (bool) {
          rename_configuration (sub_menu);
        });

      if (!is_current)
        {
          auto delete_action = new QAction {tr ("&Delete"), this};
          sub_menu->addAction (delete_action);
          connect (delete_action, &QAction::triggered, [this, sub_menu] (bool) {
              delete_configuration (sub_menu);
            });
        }
    });

  return sub_menu;
}

auto MultiSettings::impl::get_settings () const -> Dictionary
{
  Dictionary settings;
  for (auto const& key: settings_.allKeys ())
    {
      // filter out multi settings group and empty settings
      // placeholder
      if (!key.contains (multi_settings_root_group)
          && !key.contains (multi_settings_place_holder_key))
        {
          settings[key] = settings_.value (key);
        }
    }
  return settings;
}

void MultiSettings::impl::load_from (Dictionary const& dictionary, bool add_placeholder)
{
  if (dictionary.size ())
    {
      for (Dictionary::const_iterator iter = dictionary.constBegin ();
           iter != dictionary.constEnd (); ++iter)
        {
          settings_.setValue (iter.key (), iter.value ());
        }
    }
  else if (add_placeholder)
    {
      // add a placeholder key to stop the alternative configuration
      // name from disappearing
      settings_.setValue (multi_settings_place_holder_key, QVariant {});
    }
  settings_.sync ();
}

void MultiSettings::impl::select_configuration (QString const& target_name)
{
  if (main_window_ && target_name != current_)
    {
      bool changed {false};
      {
        auto const& current_group = settings_.group ();
        if (current_group.size ()) settings_.endGroup ();
        // position to the alternative settings
        SettingsGroup alternatives {&settings_, multi_settings_root_group};
        if (settings_.childGroups ().contains (target_name))
          {
            changed = true;
        // save the target settings
        SettingsGroup target_group {&settings_, target_name};
        new_settings_ = get_settings ();
          }
        if (current_group.size ()) settings_.beginGroup (current_group);
      }
      if (changed)
        {
      // and set up the restart
      current_ = target_name;
      Q_EMIT parent_->configurationNameChanged (unescape_ampersands (current_));
          restart (RepositionType::save_and_replace);
        }
    }
}

void MultiSettings::impl::clone_configuration (QMenu * parent_menu, QMenu const * menu)
{
  auto const& current_group = settings_.group ();
  if (current_group.size ()) settings_.endGroup ();
  auto const& source_name = menu->title ();

  // settings to clone
  Dictionary source_settings;
  if (source_name == current_)
    {
      // grab the data to clone from the current settings
      source_settings = get_settings ();
    }
  SettingsGroup alternatives {&settings_, multi_settings_root_group};
  if (source_name != current_)
    {
      SettingsGroup source_group {&settings_, source_name};
      source_settings = get_settings ();
    }

  // find a new unique name
  QString new_name_root {source_name + " - Copy"};;
  QString new_name {new_name_root};
  unsigned index {0};
  do
    {
      if (index++) new_name = new_name_root + '(' + QString::number (index) + ')';
    }
  while (settings_.childGroups ().contains (new_name) || new_name == current_);
  SettingsGroup new_group {&settings_, new_name};
  load_from (source_settings);

  // insert the new configuration sub menu in the parent menu
  create_sub_menu (parent_menu, new_name, configurations_group_);
  if (current_group.size ()) settings_.beginGroup (current_group);
}

void MultiSettings::impl::clone_into_configuration (QMenu const * menu)
{
  Q_ASSERT (main_window_);
  if (!main_window_) return;

  auto const& current_group = settings_.group ();
  if (current_group.size ()) settings_.endGroup ();
  auto const& target_name = menu->title ();

  // get the current configuration name
  QString current_group_name;
  QStringList sources;
  {
    SettingsGroup alternatives {&settings_, multi_settings_root_group};
    current_group_name = settings_.value (multi_settings_current_name_key).toString ();

    {
      // get the source configuration name for the clone
      sources = settings_.childGroups ();
      sources << current_group_name;
      sources.removeOne (target_name);
    }
  }

  // pick a source configuration
  ExistingNameDialog dialog {sources, main_window_};
  if (sources.size () && (1 == sources.size () || QDialog::Accepted == dialog.exec ()))
    {
      QString source_name {1 == sources.size () ? sources.at (0) : dialog.name ()};
      if (main_window_
          && MessageBox::Yes == MessageBox::query_message (main_window_,
                                                        tr ("Clone Into Configuration"),
                                                        tr ("Confirm overwrite of all values for configuration \"%1\" with values from \"%2\"?")
                                                        .arg (unescape_ampersands (target_name))
                                                        .arg (unescape_ampersands (source_name))))
        {
          // grab the data to clone from
          if (source_name == current_group_name)
            {
              // grab the data to clone from the current settings
              new_settings_ = get_settings ();
            }
          else
            {
              SettingsGroup alternatives {&settings_, multi_settings_root_group};
              SettingsGroup source_group {&settings_, source_name};
              new_settings_ = get_settings ();
            }

          // purge target settings and replace
          if (target_name == current_)
            {
              // restart with new settings
              restart (RepositionType::replace);
            }
          else
            {
              SettingsGroup alternatives {&settings_, multi_settings_root_group};
              SettingsGroup target_group {&settings_, target_name};
              settings_.remove (QString {}); // purge entire group
              load_from (new_settings_);
              new_settings_.clear ();
            }
        }
    }
  if (current_group.size ()) settings_.beginGroup (current_group);
}

void MultiSettings::impl::reset_configuration (QMenu const * menu)
{
  Q_ASSERT (main_window_);
  if (!main_window_) return;

  auto const& target_name = menu->title ();

  if (!main_window_
      || MessageBox::Yes != MessageBox::query_message (main_window_,
                                                    tr ("Reset Configuration"),
                                                    tr ("Confirm reset to default values for configuration \"%1\"?")
                                                    .arg (unescape_ampersands (target_name))))
    {
      return;
    }

  if (target_name == current_)
    {
      // restart with default settings
      new_settings_.clear ();
      restart (RepositionType::replace);
    }
  else
    {
      auto const& current_group = settings_.group ();
      if (current_group.size ()) settings_.endGroup ();
      SettingsGroup alternatives {&settings_, multi_settings_root_group};
      SettingsGroup target_group {&settings_, target_name};
      settings_.remove (QString {}); // purge entire group
      // add a placeholder to stop alternative configuration name
      // being lost
      settings_.setValue (multi_settings_place_holder_key, QVariant {});
      settings_.sync ();
      if (current_group.size ()) settings_.beginGroup (current_group);
    }
}

void MultiSettings::impl::rename_configuration (QMenu * menu)
{
  Q_ASSERT (main_window_);
  if (!main_window_) return;

  auto const& current_group = settings_.group ();
  if (current_group.size ()) settings_.endGroup ();
  auto const& target_name = menu->title ();

  // gather names we cannot use
  SettingsGroup alternatives {&settings_, multi_settings_root_group};
  auto invalid_names = settings_.childGroups ();
  invalid_names << settings_.value (multi_settings_current_name_key).toString ();

  // get the new name
  NameDialog dialog {target_name, invalid_names, main_window_};
  if (QDialog::Accepted == dialog.exec ())
    {
      if (target_name == current_)
        {
          settings_.setValue (multi_settings_current_name_key, dialog.new_name ());
          settings_.sync ();
          current_ = dialog.new_name ();
          Q_EMIT parent_->configurationNameChanged (unescape_ampersands (current_));
        }
      else
        {
          // switch to the target group and fetch the configuration data
          Dictionary target_settings;
          {
            // grab the target configuration settings
            SettingsGroup target_group {&settings_, target_name};
            target_settings = get_settings ();
            // purge the old configuration data
            settings_.remove (QString {}); // purge entire group
          }
          // load into new configuration group name
          SettingsGroup target_group {&settings_, dialog.new_name ()};
          load_from (target_settings);
        }
      // change the action text in the menu
      menu->setTitle (dialog.new_name ());
    }
  if (current_group.size ()) settings_.beginGroup (current_group);
}

void MultiSettings::impl::delete_configuration (QMenu * menu)
{
  Q_ASSERT (main_window_);
  auto const& target_name = menu->title ();

  if (target_name == current_)
    {
      return;               // suicide not allowed here
    }
  else
    {
      if (!main_window_
          || MessageBox::Yes != MessageBox::query_message (main_window_,
                                                        tr ("Delete Configuration"),
                                                        tr ("Confirm deletion of configuration \"%1\"?")
                                                        .arg (unescape_ampersands (target_name))))
        {
          return;
        }
      auto const& current_group = settings_.group ();
      if (current_group.size ()) settings_.endGroup ();
      SettingsGroup alternatives {&settings_, multi_settings_root_group};
      SettingsGroup target_group {&settings_, target_name};
      // purge the configuration data
      settings_.remove (QString {}); // purge entire group
      settings_.sync ();
      if (current_group.size ()) settings_.beginGroup (current_group);
    }
  // update the menu
  menu->deleteLater ();
}

void MultiSettings::impl::restart (RepositionType type)
{
  Q_ASSERT (main_window_);
  reposition_type_ = type;
  exit_flag_ = false;
  main_window_->close ();
  main_window_ = nullptr;
}
