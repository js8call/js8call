#include <QLoggingCategory>
#include "EmulateSplitTransceiver.hpp"

Q_DECLARE_LOGGING_CATEGORY(emulatesplittransceiver_js8)

EmulateSplitTransceiver::EmulateSplitTransceiver (std::unique_ptr<Transceiver> wrapped, QObject * parent)
  : Transceiver {parent}
  , wrapped_ {std::move (wrapped)}
  , rx_frequency_ {0}
  , tx_frequency_ {0}
  , split_ {false}
{
  // Connect update signal of wrapped Transceiver object instance to ours.
  connect (wrapped_.get (), &Transceiver::update, this, &EmulateSplitTransceiver::handle_update);

  // Connect other signals of wrapped Transceiver object to our
  // parent matching signals.
  connect (wrapped_.get (), &Transceiver::resolution, this, &Transceiver::resolution);
  connect (wrapped_.get (), &Transceiver::finished, this, &Transceiver::finished);
  connect (wrapped_.get (), &Transceiver::failure, this, &Transceiver::failure);
}

void EmulateSplitTransceiver::set (TransceiverState const& s, unsigned sequence_number) noexcept
{
  qCDebug (emulatesplittransceiver_js8) << "EmulateSplitTransceiver::set: state:" << s << "#:" << sequence_number;

  // save for use in updates
  rx_frequency_ = s.frequency ();
  tx_frequency_ = s.tx_frequency ();
  split_ = s.split ();

  TransceiverState emulated_state {s};
  if (s.ptt () && split_) emulated_state.frequency (s.tx_frequency ());
  emulated_state.split (false);
  emulated_state.tx_frequency (0);
  wrapped_->set (emulated_state, sequence_number);
}

void EmulateSplitTransceiver::handle_update (TransceiverState const& state,
                                             unsigned sequence_number)
{
  qCDebug (emulatesplittransceiver_js8) << "EmulateSplitTransceiver::handle_update: from wrapped:" << state;

  if (state.split ())
    {
      Q_EMIT failure (tr ("Emulated split mode requires rig to be in simplex mode"));
    }
  else
    {
      TransceiverState new_state {state};
      // Follow the rig if in RX mode.
      if (state.ptt ()) new_state.frequency (rx_frequency_);

      // These are always what was requested in prior set state operation
      new_state.tx_frequency (tx_frequency_);
      new_state.split (split_);

      qCDebug (emulatesplittransceiver_js8) << "EmulateSplitTransceiver::handle_update: signalling:" << state;

      // signal emulated state
      Q_EMIT update (new_state, sequence_number);
    }
}

Q_LOGGING_CATEGORY(emulatesplittransceiver_js8, "emulatesplittransceiver.js8", QtWarningMsg)
