#include "TraceFile.hpp"

#include <stdexcept>

#include <QDebug>
#include <QString>
#include <QFile>
#include <QTextStream>
#include <QMessageLogContext>
#include <QDateTime>
#include <QMutex>
#include <QMutexLocker>

#include "pimpl_impl.hpp"

namespace
{
  QMutex lock;
}

class TraceFile::impl
{
public:
  impl (QString const& trace_file_path);
  ~impl ();

  // no copying
  impl (impl const&) = delete;
  impl& operator = (impl const&) = delete;

private:
  // write Qt messages to the diagnostic log file
  static void message_handler (QtMsgType type, QMessageLogContext const& context, QString const& msg);

  QFile file_;
  QTextStream stream_;
  QTextStream * original_stream_;
  QtMessageHandler original_handler_;
  static QTextStream * current_stream_;
};

QTextStream * TraceFile::impl::current_stream_;


// delegate to implementation class
TraceFile::TraceFile (QString const& trace_file_path)
  : m_ {trace_file_path}
{
}

TraceFile::~TraceFile ()
{
}


TraceFile::impl::impl (QString const& trace_file_path)
  : file_ {trace_file_path}
  , original_stream_ {current_stream_}
  , original_handler_ {nullptr}
{
  // if the log file is writeable; initialise diagnostic logging to it
  // for append and hook up the Qt global message handler
  if (file_.open (QFile::WriteOnly | QFile::Append | QFile::Text))
    {
      stream_.setDevice (&file_);
      current_stream_ = &stream_;
      original_handler_ = qInstallMessageHandler (message_handler);
    }
}

TraceFile::impl::~impl ()
{
  // unhook our message handler before the stream and file are destroyed
  if (original_handler_)
    {
      qInstallMessageHandler (original_handler_);
    }
  current_stream_ = original_stream_; // revert to prior stream
}

// write Qt messages to the diagnostic log file
void TraceFile::impl::message_handler (QtMsgType type, QMessageLogContext const& context, QString const& msg)
{
  char const * severity;
  switch (type)
    {
    case QtDebugMsg:
      severity = "Debug";
      break;

    case QtWarningMsg:
      severity = "Warning";
      break;

    case QtFatalMsg:
      severity = "Fatal";
      break;

    default:
      severity = "Critical";
      break;
    }

  {
    // guard against multiple threads with overlapping messages
    QMutexLocker guard (&lock);
    Q_ASSERT_X (current_stream_, "TraceFile:message_handler", "no stream to write to");
    *current_stream_
      << QDateTime::currentDateTimeUtc ().toString ("yyyy-MM-ddTHH:mm:ss.zzzZ")
      << '(' << context.file << ':' << context.line /* << ", " << context.function */ << ')'
      << severity << ": " << msg.trimmed () << Qt::endl;
  }

  if (QtFatalMsg == type)
    {
      throw std::runtime_error {"Fatal Qt Error"};
    }
}
