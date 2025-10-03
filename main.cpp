#include <iostream>
#include <exception>
#include <stdexcept>
#include <string>

#include <locale.h>
#include <fftw3.h>

#include <QDateTime>
#include <QApplication>
#include <QLoggingCategory>
#include <QRegularExpression>
#include <QObject>
#include <QSettings>
#include <QLibraryInfo>
#include <QSysInfo>
#include <QDir>
#include <QStandardPaths>
#include <QStringList>
#include <QLockFile>
#include <QStack>

#if QT_VERSION >= 0x050200
#include <QCommandLineParser>
#include <QCommandLineOption>
#endif

#include "revision_utils.hpp"
#include "MetaDataRegistry.hpp"
#include "SettingsGroup.hpp"
#include "TraceFile.hpp"
#include "MultiSettings.hpp"
#include "mainwindow.h"
#include "commons.h"
#include "Radio.hpp"
#include "FrequencyList.hpp"
#include "MessageBox.hpp"       // last to avoid nasty MS macro definitions

#include "DriftingDateTime.h"

Q_DECLARE_LOGGING_CATEGORY(main_js8)

namespace
{
  class MessageTimestamper
  {
  public:
    MessageTimestamper ()
    {
      prior_handlers_.push (qInstallMessageHandler (message_handler));
    }
    ~MessageTimestamper ()
    {
      if (prior_handlers_.size ()) qInstallMessageHandler (prior_handlers_.pop ());
    }

  private:
    static void message_handler (QtMsgType type, QMessageLogContext const& context, QString const& msg)
    {
      QtMessageHandler handler {prior_handlers_.top ()};
      if (handler)
        {
          handler (type, context,
                   DriftingDateTime::currentDateTimeUtc ().toString ("yy-MM-ddTHH:mm:ss.zzzZ: ") + msg);
        }
    }
    static QStack<QtMessageHandler> prior_handlers_;
  };
  QStack<QtMessageHandler> MessageTimestamper::prior_handlers_;
}

int main(int argc, char *argv[])
{
  // Add timestamps to all debug messages
  MessageTimestamper message_timestamper;

  // make the Qt type magic happen
  Radio::register_types ();
  register_types ();

  QApplication a(argc, argv);
  try
    {
      setlocale (LC_NUMERIC, "C"); // ensure number forms are in
                                   // consistent format, do this after
                                   // instantiating QApplication so
                                   // that GUI has correct l18n

      // Override programs executable basename as application name.
      a.setApplicationName("JS8Call");
      a.setApplicationVersion (version());

#if QT_VERSION >= 0x050200
      QCommandLineParser parser;
      parser.setApplicationDescription ("\n" PROJECT_SUMMARY_DESCRIPTION);
      auto help_option = parser.addHelpOption ();
      auto version_option = parser.addVersionOption ();

      QCommandLineOption output_option(QStringList() << "o" << "output", "Write debug statements into <file>.", "file");
      parser.addOption (output_option);

      // support for multiple instances running from a single installation
      QCommandLineOption rig_option (QStringList {} << "r" << "rig-name"
                                     , a.translate ("main", "Where <rig-name> is for multi-instance support.")
                                     , a.translate ("main", "rig-name"));
      parser.addOption (rig_option);

      // support for start up configuration
      QCommandLineOption cfg_option (QStringList {} << "c" << "config"
                                     , a.translate ("main", "Where <configuration> is an existing one.")
                                     , a.translate ("main", "configuration"));
      parser.addOption (cfg_option);

      QCommandLineOption test_option (QStringList {} << "test-mode"
                                      , a.translate ("main", "Writable files in test location.  Use with caution, for testing only."));
      parser.addOption (test_option);

      if (!parser.parse (a.arguments ()))
        {
          std::cerr << parser.errorText().toLocal8Bit ().data () << std::endl;
          return -1;
        }
      else
        {
          if (parser.isSet (help_option))
            {
              parser.showHelp (-1);
              return 0;
            }
          else if (parser.isSet (version_option))
            {
              parser.showVersion();
              return 0;
            }
        }

      if(parser.isSet(output_option)){
          new TraceFile(parser.value(output_option));
      }

      QStandardPaths::setTestModeEnabled (parser.isSet (test_option));

      // support for multiple instances running from a single installation
      bool multiple {false};
      if (parser.isSet (rig_option) || parser.isSet (test_option))
        {
          auto temp_name = parser.value (rig_option);
          if (!temp_name.isEmpty ())
            {
              if (temp_name.contains (QRegularExpression {R"([\\/,])"}))
                {
                  std::cerr << "Invalid rig name - \\ & / not allowed" << std::endl;
                  parser.showHelp (-1);
                }
                
              a.setApplicationName (a.applicationName () + " - " + temp_name);
            }

          if (parser.isSet (test_option))
            {
              a.setApplicationName (a.applicationName () + " - test");
            }

          multiple = true;
        }

      // now we have the application name we can open the settings
      MultiSettings multi_settings {parser.value (cfg_option)};

      // find the temporary files path
      QDir temp_dir {QStandardPaths::writableLocation (QStandardPaths::TempLocation)};
      Q_ASSERT (temp_dir.exists ()); // sanity check

      // disallow multiple instances with same instance key
      QLockFile instance_lock {temp_dir.absoluteFilePath (a.applicationName () + ".lock")};
      instance_lock.setStaleLockTime (0);
      while (!instance_lock.tryLock ())
        {
          if (QLockFile::LockFailedError == instance_lock.error ())
            {
              switch (MessageBox::query_message (nullptr
                                                 , a.translate ("main", "Another instance may be running")
                                                 , a.translate ("main", "try to remove stale lock file?")
                                                 , QString {}
                                                 , MessageBox::Yes | MessageBox::Retry | MessageBox::No
                                                 , MessageBox::Yes))
                {
                case MessageBox::Yes:
                  instance_lock.removeStaleLockFile ();
                  break;

                case MessageBox::Retry:
                  break;

                default:
                  throw std::runtime_error {"Multiple instances must have unique rig names"};
                }
            }
          else
            {
              throw std::runtime_error {"Failed to access lock file"};
            }
        }
#endif

#if WSJT_QDEBUG_TO_FILE
      // Open a trace file
      TraceFile trace_file {temp_dir.absoluteFilePath (a.applicationName () + "_trace.log")};
      qCDebug (main_js8) << program_title () + " - Program startup";
#endif

      // Create a unique writeable temporary directory in a suitable location
      bool temp_ok {false};
      QString unique_directory {QApplication::applicationName ()};
      do
        {
          if (!temp_dir.mkpath (unique_directory)
              || !temp_dir.cd (unique_directory))
            {
              MessageBox::critical_message (nullptr,
                                            a.translate ("main", "Failed to create a temporary directory"),
                                            a.translate ("main", "Path: \"%1\"").arg (temp_dir.absolutePath ()));
              throw std::runtime_error {"Failed to create a temporary directory"};
            }
          if (!temp_dir.isReadable () || !(temp_ok = QTemporaryFile {temp_dir.absoluteFilePath ("test")}.open ()))
            {
              auto button =  MessageBox::critical_message (nullptr,
                                                           a.translate ("main", "Failed to create a usable temporary directory"),
                                                           a.translate ("main", "Another application may be locking the directory"),
                                                           a.translate ("main", "Path: \"%1\"").arg (temp_dir.absolutePath ()),
                                                           MessageBox::Retry | MessageBox::Cancel);
              if (MessageBox::Cancel == button)
                {
                  throw std::runtime_error {"Failed to create a usable temporary directory"};
                }
              temp_dir.cdUp ();  // revert to parent as this one is no good
            }
        }
      while (!temp_ok);

      int result;
      do
        {
#if WSJT_QDEBUG_TO_FILE
          // announce to trace file and dump settings
          qCDebug (main_js8) << "++++++++++++++++++++++++++++ Settings ++++++++++++++++++++++++++++";
          for (auto const& key: multi_settings.settings ()->allKeys ())
            {
              auto const& value = multi_settings.settings ()->value (key);
              if (value.canConvert<QVariantList> ())
                {
                  auto const sequence = value.value<QSequentialIterable> ();
                  qCDebug (main_js8).nospace () << key << ": ";
                  for (auto const& item: sequence)
                    {
                      qCDebug (main_js8).nospace () << '\t' << item;
                    }
                }
              else
                {
                  qCDebug (main_js8).nospace () << key << ": " << value;
                }
            }
          qCDebug (main_js8) << "---------------------------- Settings ----------------------------";
#endif

          // run the application UI
          MainWindow w(program_version(), temp_dir, multiple, &multi_settings);
          w.show();
          result = a.exec();
        }
      while (!result && !multi_settings.exit ());

      fftwf_forget_wisdom ();
      fftwf_cleanup ();

      temp_dir.removeRecursively (); // clean up temp files
      return result;
    }
  catch (std::exception const& e)
    {
      MessageBox::critical_message (nullptr, "Fatal error", e.what ());
      std::cerr << "Error: " << e.what () << '\n';
    }
  catch (...)
    {
      MessageBox::critical_message (nullptr, "Unexpected fatal error");
      std::cerr << "Unexpected fatal error\n";
      throw;			// hoping the runtime might tell us more about the exception
    }
  return -1;
}

Q_LOGGING_CATEGORY(main_js8, "main.js8", QtWarningMsg)
