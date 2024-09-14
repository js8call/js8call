#ifndef PSK_REPORTER_HPP_
#define PSK_REPORTER_HPP_

#include <QObject>
#include "Radio.hpp"
#include "pimpl_h.hpp"

class QString;
class Configuration;

class PSKReporter final
  : public QObject
{
  Q_OBJECT

public:
  explicit PSKReporter (Configuration const *, QString const& program_info);
  ~PSKReporter ();

  void reconnect ();

  void setLocalStation (QString const& call, QString const& grid, QString const& antenna);

  //
  // Returns false if PSK Reporter connection is not available
  //
  bool addRemoteStation (QString const& call, QString const& grid, Radio::Frequency freq, QString const& mode, int snr);

  //
  // Flush any pending spots to PSK Reporter
  //
  void sendReport (bool last = false);

  Q_SIGNAL void errorOccurred (QString const& reason);

private:
  class impl;
  pimpl<impl> m_;
};

#endif
