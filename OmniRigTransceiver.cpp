#include "OmniRigTransceiver.hpp"

#include <QTimer>
#include <QDebug>
#include <objbase.h>
#include <QThread>

#include "qt_helpers.hpp"

#include "moc_OmniRigTransceiver.cpp"

namespace
{
  auto constexpr OmniRig_transceiver_one_name = "OmniRig Rig 1";
  auto constexpr OmniRig_transceiver_two_name = "OmniRig Rig 2";
}

auto OmniRigTransceiver::map_mode (OmniRig::RigParamX param) -> MODE
{
  if (param & OmniRig::PM_CW_U)
    {
      return CW_R;
    }
  else if (param & OmniRig::PM_CW_L)
    {
      return CW;
    }
  else if (param & OmniRig::PM_SSB_U)
    {
      return USB;
    }
  else if (param & OmniRig::PM_SSB_L)
    {
      return LSB;
    }
  else if (param & OmniRig::PM_DIG_U)
    {
      return DIG_U;
    }
  else if (param & OmniRig::PM_DIG_L)
    {
      return DIG_L;
    }
  else if (param & OmniRig::PM_AM)
    {
      return AM;
    }
  else if (param & OmniRig::PM_FM)
    {
      return FM;
    }
  TRACE_CAT ("OmniRigTransceiver", "unrecognized mode");
  throw_qstring (tr ("OmniRig: unrecognized mode"));
  return UNK;
}

OmniRig::RigParamX OmniRigTransceiver::map_mode (MODE mode)
{
  switch (mode)
    {
    case AM: return OmniRig::PM_AM;
    case CW: return OmniRig::PM_CW_L;
    case CW_R: return OmniRig::PM_CW_U;
    case USB: return OmniRig::PM_SSB_U;
    case LSB: return OmniRig::PM_SSB_L;
    case FSK: return OmniRig::PM_DIG_L;
    case FSK_R: return OmniRig::PM_DIG_U;
    case DIG_L: return OmniRig::PM_DIG_L;
    case DIG_U: return OmniRig::PM_DIG_U;
    case FM: return OmniRig::PM_FM;
    case DIG_FM: return OmniRig::PM_FM;
    default: break;
    }
  return OmniRig::PM_SSB_U; // quieten compiler grumble
}

void OmniRigTransceiver::register_transceivers (TransceiverFactory::Transceivers * registry, int id1, int id2)
{
  (*registry)[OmniRig_transceiver_one_name] = TransceiverFactory::Capabilities {
    id1
    , TransceiverFactory::Capabilities::none // COM isn't serial or network
    , true             // does PTT
    , false            // doesn't select mic/data (use OmniRig config file)
    , true             // can remote control RTS nd DTR
    , true             // asynchronous interface
  };
  (*registry)[OmniRig_transceiver_two_name] = TransceiverFactory::Capabilities {
    id2
    , TransceiverFactory::Capabilities::none // COM isn't serial or network
    , true             // does PTT
    , false            // doesn't select mic/data (use OmniRig config file)
    , true             // can remote control RTS nd DTR
    , true             // asynchronous interface
  };
}

OmniRigTransceiver::OmniRigTransceiver (std::unique_ptr<TransceiverBase> wrapped,
                                        RigNumber n, TransceiverFactory::PTTMethod ptt_type,
                                        QString const& ptt_port, QObject * parent)
  : TransceiverBase {parent}
  , wrapped_ {std::move (wrapped)}
  , use_for_ptt_ {TransceiverFactory::PTT_method_CAT == ptt_type || ("CAT" == ptt_port && (TransceiverFactory::PTT_method_RTS == ptt_type || TransceiverFactory::PTT_method_DTR == ptt_type))}
  , ptt_type_ {ptt_type}
  , rig_number_ {n}
  , readable_params_ {0}
  , writable_params_ {0}
  , send_update_signal_ {false}
  , reversed_ {false}
{
}

int OmniRigTransceiver::do_start ()
{
  TRACE_CAT ("OmniRigTransceiver", "starting");
  if (wrapped_) wrapped_->start (0);

  CoInitializeEx (nullptr, 0 /*COINIT_APARTMENTTHREADED*/); // required because Qt only does this for GUI thread

  omni_rig_.reset (new OmniRig::OmniRigX {this});
  if (omni_rig_->isNull ())
    {
      TRACE_CAT ("OmniRigTransceiver", "failed to start COM server");
      throw_qstring (tr ("Failed to start OmniRig COM server"));
    }

  // COM/OLE exceptions get signaled
  connect (&*omni_rig_, SIGNAL (exception (int, QString, QString, QString)), this, SLOT (handle_COM_exception (int, QString, QString, QString)));

  // IOmniRigXEvent interface signals
  connect (&*omni_rig_, SIGNAL (VisibleChange ()), this, SLOT (handle_visible_change ()));
  connect (&*omni_rig_, SIGNAL (RigTypeChange (int)), this, SLOT (handle_rig_type_change (int)));
  connect (&*omni_rig_, SIGNAL (StatusChange (int)), this, SLOT (handle_status_change (int)));
  connect (&*omni_rig_, SIGNAL (ParamsChange (int, int)), this, SLOT (handle_params_change (int, int)));
  connect (&*omni_rig_
           , SIGNAL (CustomReply (int, QVariant const&, QVariant const&))
           , this, SLOT (handle_custom_reply (int, QVariant const&, QVariant const&)));

  TRACE_CAT ("OmniRigTransceiver", "OmniRig s/w version:" << QString::number (omni_rig_->SoftwareVersion ()).toLocal8Bit ()
             << "i/f version:" << QString::number (omni_rig_->InterfaceVersion ()).toLocal8Bit ());

  // fetch the interface of the RigX CoClass and instantiate a proxy object
  switch (rig_number_)
    {
    case One: rig_.reset (new OmniRig::RigX (omni_rig_->Rig1 ())); break;
    case Two: rig_.reset (new OmniRig::RigX (omni_rig_->Rig2 ())); break;
    }

  Q_ASSERT (rig_);
  Q_ASSERT (!rig_->isNull ());

  if (use_for_ptt_ && (TransceiverFactory::PTT_method_DTR == ptt_type_ || TransceiverFactory::PTT_method_RTS == ptt_type_))
    {
      // fetch the interface for the serial port if we need it for PTT
      port_.reset (new OmniRig::PortBits (rig_->PortBits ()));

      Q_ASSERT (port_);
      Q_ASSERT (!port_->isNull ());
      TRACE_CAT ("OmniRigTransceiver", "OmniRig RTS state:" << port_->Rts ());

      if (!port_->Lock ()) // try to take exclusive use of the OmniRig serial port for PTT
        {
          TRACE_CAT ("OmniRigTransceiver", "Failed to get exclusive use of serial port for PTT from OmniRig");
        }

      // start off so we don't accidentally key the radio
      if (TransceiverFactory::PTT_method_DTR == ptt_type_)
        {
          port_->SetDtr (false);
        }
      else      // RTS
        {
          port_->SetRts (false);
        }
    }

  rig_type_ = rig_->RigType ();
  readable_params_ = rig_->ReadableParams ();
  writable_params_ = rig_->WriteableParams ();

  TRACE_CAT ("OmniRigTransceiver", QString {"OmniRig initial rig type: %1 readable params = 0x%2 writable params = 0x%3 for rig %4"}
    .arg (rig_type_)
    .arg (readable_params_, 8, 16, QChar ('0'))
    .arg (writable_params_, 8, 16, QChar ('0'))
    .arg (rig_number_).toLocal8Bit ());

  offline_timer_.reset (new QTimer);
  offline_timer_->setSingleShot (true);
  offline_timer_->setInterval (5 * 1000);
  connect (&*offline_timer_, &QTimer::timeout, this, &OmniRigTransceiver::timeout_check);

  for (unsigned tries {0}; tries < 10; ++tries)
    {
      QThread::msleep (100);    // wait until OmniRig polls the rig
      auto f = rig_->GetRxFrequency ();
      int resolution {0};
      if (f)
        {
          if (OmniRig::PM_UNKNOWN == rig_->Vfo ()
              && (writable_params_ & (OmniRig::PM_VFOA | OmniRig::PM_VFOB))
              == (OmniRig::PM_VFOA | OmniRig::PM_VFOB))
            {
              // start with VFO A (probably MAIN) on rigs that we
              // can't query VFO but can set explicitly
              rig_->SetVfo (OmniRig::PM_VFOA);
            }
          if (f % 10) return resolution; // 1Hz resolution
          auto test_frequency = f - f % 100 + 55;
          if (OmniRig::PM_FREQ & writable_params_)
            {
              rig_->SetFreq (test_frequency);
            }
          else if (reversed_ && (OmniRig::PM_FREQB & writable_params_))
            {
              rig_->SetFreqB (test_frequency);
            }
          else if (!reversed_ && (OmniRig::PM_FREQA & writable_params_))
            {
              rig_->SetFreqA (test_frequency);
            }
          else
            {
              throw_qstring (tr ("OmniRig: don't know how to set rig frequency"));
            }
          switch (rig_->GetRxFrequency () - test_frequency)
            {
            case -5: resolution = -1; break;  // 10Hz truncated
            case 5: resolution = 1; break;    // 10Hz rounded
            case -15: resolution = -2; break; // 20Hz truncated
            case -55: resolution = -2; break; // 100Hz truncated
            case 45: resolution = 2; break;   // 100Hz rounded
            }
          if (1 == resolution)  // may be 20Hz rounded
            {
              test_frequency = f - f % 100 + 51;
              if (OmniRig::PM_FREQ & writable_params_)
                {
                  rig_->SetFreq (test_frequency);
                }
              else if (reversed_ && (OmniRig::PM_FREQB & writable_params_))
                {
                  rig_->SetFreqB (test_frequency);
                }
              else if (!reversed_ && (OmniRig::PM_FREQA & writable_params_))
                {
                  rig_->SetFreqA (test_frequency);
                }
              if (9 == rig_->GetRxFrequency () - test_frequency)
                {
                  resolution = 2;   // 20Hz rounded
                }
            }
          if (OmniRig::PM_FREQ & writable_params_)
            {
              rig_->SetFreq (f);
            }
          else if (reversed_ && (OmniRig::PM_FREQB & writable_params_))
            {
              rig_->SetFreqB (f);
            }
          else if (!reversed_ && (OmniRig::PM_FREQA & writable_params_))
            {
              rig_->SetFreqA (f);
            }
          update_rx_frequency (f);
          return resolution;
        }
    }
  throw_qstring (tr ("OmniRig: Initialization timed out"));
  return 0;                     // keep compiler happy
}

void OmniRigTransceiver::do_stop ()
{
  if (offline_timer_)
    {
      offline_timer_->stop ();
      offline_timer_.reset ();
    }

  QThread::msleep (200);        // leave some time for pending
                                // commands at the server end
  if (port_)
    {
      port_->Unlock ();   // release serial port
      port_->clear ();
      port_.reset ();
    }
  if (omni_rig_)
    {
      if (rig_)
        {
          rig_->clear ();
          rig_.reset ();
        }
      omni_rig_->clear ();
      omni_rig_.reset ();
      CoUninitialize ();
    }
  if (wrapped_) wrapped_->stop ();
  TRACE_CAT ("OmniRigTransceiver", "stopped");
}

void OmniRigTransceiver::do_sync (bool force_signal, bool /*no_poll*/)
{
  // nothing much we can do here, we just have to let OmniRig do its
  // stuff and its first poll should send us and update that will
  // trigger a update signal from us. Any attempt to query OmniRig
  // leads to a whole mess of trouble since its internal state is
  // garbage until it has done its first rig poll.
  send_update_signal_ = force_signal;
  update_complete ();
}

void OmniRigTransceiver::handle_COM_exception (int code, QString source, QString desc, QString help)
{
  TRACE_CAT ("OmniRigTransceiver", QString::number (code) + " at " + source + ": " + desc + " (" + help + ')');
  throw_qstring (tr ("OmniRig COM/OLE error: %1 at %2: %3 (%4)").arg (QString::number (code)).arg (source). arg (desc). arg (help));
}

void OmniRigTransceiver::handle_visible_change ()
{
  TRACE_CAT ("OmniRigTransceiver", "visibility change: visibility =" << omni_rig_->DialogVisible ());
}

void OmniRigTransceiver::handle_rig_type_change (int rig_number)
{
  if (rig_number_ == rig_number)
    {
      readable_params_ = rig_->ReadableParams ();
      writable_params_ = rig_->WriteableParams ();
      TRACE_CAT ("OmniRigTransceiver", QString {"OmniRig rig type change to: %1 readable params = 0x%2 writable params = 0x%3 for rig %4"}
        .arg (rig_->RigType ())
        .arg (readable_params_, 8, 16, QChar ('0'))
        .arg (writable_params_, 8, 16, QChar ('0'))
        .arg (rig_number).toLocal8Bit ());
    }
}

void OmniRigTransceiver::handle_status_change (int rig_number)
{
  if (rig_number_ == rig_number)
    {
      auto const& status = rig_->StatusStr ().toLocal8Bit ();
      TRACE_CAT ("OmniRigTransceiver", QString {"OmniRig status change: new status for rig %1 = "}.arg (rig_number).toLocal8Bit () << status);
      if (OmniRig::ST_ONLINE != rig_->Status ())
        {
          if (!offline_timer_->isActive ())
            {
              offline_timer_->start (); // give OmniRig time to recover
            }
        }
      else
        {
          offline_timer_->stop ();
          update_rx_frequency (rig_->GetRxFrequency ());
          update_complete ();
          TRACE_CAT ("OmniRigTransceiver", "OmniRig frequency:" << state ().frequency ());
        }
    }
}

void OmniRigTransceiver::timeout_check ()
{
  offline ("Rig went offline");
}

void OmniRigTransceiver::handle_params_change (int rig_number, int params)
{
  if (rig_number_ == rig_number)
    {
      TRACE_CAT ("OmniRigTransceiver", QString {"OmniRig params change: params = 0x%1 for rig %2"}
        .arg (params, 8, 16, QChar ('0'))
        .arg (rig_number).toLocal8Bit ()
        << "state before:" << state ());
      //      starting_ = false;
      TransceiverState old_state {state ()};
      auto need_frequency = false;
      // state_.online = true;  // sometimes we don't get an initial
      //        // OmniRig::ST_ONLINE status change
      //        // event
      if (params & OmniRig::PM_VFOAA)
        {
          update_split (false);
          reversed_ = false;
          update_rx_frequency (rig_->FreqA ());
          update_other_frequency (rig_->FreqB ());
        }
      if (params & OmniRig::PM_VFOAB)
        {
          update_split (true);
          reversed_ = false;
          update_rx_frequency (rig_->FreqA ());
          update_other_frequency (rig_->FreqB ());
        }
      if (params & OmniRig::PM_VFOBA)
        {
          update_split (true);
          reversed_ = true;
          update_other_frequency (rig_->FreqA ());
          update_rx_frequency (rig_->FreqB ());
        }
      if (params & OmniRig::PM_VFOBB)
        {
          update_split (false);
          reversed_ = true;
          update_other_frequency (rig_->FreqA ());
          update_rx_frequency (rig_->FreqB ());
        }
      if (params & OmniRig::PM_VFOA)
        {
          reversed_ = false;
          need_frequency = true;
        }
      if (params & OmniRig::PM_VFOB)
        {
          reversed_ = true;
          need_frequency = true;
        }

      if (params & OmniRig::PM_FREQ)
        {
          need_frequency = true;
        }
      if (params & OmniRig::PM_FREQA)
        {
          if (reversed_)
            {
              update_other_frequency (rig_->FreqA ());
            }
          else
            {
              update_rx_frequency (rig_->FreqA ());
            }
        }
      if (params & OmniRig::PM_FREQB)
        {
          if (reversed_)
            {
              update_rx_frequency (rig_->FreqB ());
            }
          else
            {
              update_other_frequency (rig_->FreqB ());
            }
        }
      if (need_frequency)
        {
          if (readable_params_ & OmniRig::PM_FREQA)
            {
              if (reversed_)
                {
                  update_other_frequency (rig_->FreqA ());
                }
              else
                {
                  update_rx_frequency (rig_->FreqA ());
                }
              need_frequency = false;
            }
          if (readable_params_ & OmniRig::PM_FREQB)
            {
              if (reversed_)
                {
                  update_rx_frequency (rig_->FreqB ());
                }
              else
                {
                  update_other_frequency (rig_->FreqB ());
                }
              need_frequency = false;
            }
        }
      if (need_frequency && (readable_params_ & OmniRig::PM_FREQ)
          && !state ().ptt ())
        {
          update_rx_frequency (rig_->Freq ());
        }
      if (params & OmniRig::PM_PITCH)
        {
        }
      if (params & OmniRig::PM_RITOFFSET)
        {
        }
      if (params & OmniRig::PM_RIT0)
        {
        }
      if (params & OmniRig::PM_VFOEQUAL)
        {
          auto f = readable_params_ & OmniRig::PM_FREQA ? rig_->FreqA () : rig_->Freq ();
          update_rx_frequency (f);
          update_other_frequency (f);
          update_mode (map_mode (rig_->Mode ()));
        }
      if (params & OmniRig::PM_VFOSWAP)
        {
          auto temp = state ().tx_frequency ();
          update_other_frequency (state ().frequency ());
          update_rx_frequency (temp);
          update_mode (map_mode (rig_->Mode ()));
        }
      if (params & OmniRig::PM_SPLITON)
        {
          update_split (true);
        }
      if (params & OmniRig::PM_SPLITOFF)
        {
          update_split (false);
        }
      if (params & OmniRig::PM_RITON)
        {
        }
      if (params & OmniRig::PM_RITOFF)
        {
        }
      if (params & OmniRig::PM_XITON)
        {
        }
      if (params & OmniRig::PM_XITOFF)
        {
        }
      if (params & OmniRig::PM_RX)
        {
          update_PTT (false);
        }
      if (params & OmniRig::PM_TX)
        {
          update_PTT ();
        }
      if (params & OmniRig::PM_CW_U)
        {
          update_mode (CW_R);
        }
      if (params & OmniRig::PM_CW_L)
        {
          update_mode (CW);
        }
      if (params & OmniRig::PM_SSB_U)
        {
          update_mode (USB);
        }
      if (params & OmniRig::PM_SSB_L)
        {
          update_mode (LSB);
        }
      if (params & OmniRig::PM_DIG_U)
        {
          update_mode (DIG_U);
        }
      if (params & OmniRig::PM_DIG_L)
        {
          update_mode (DIG_L);
        }
      if (params & OmniRig::PM_AM)
        {
          update_mode (AM);
        }
      if (params & OmniRig::PM_FM)
        {
          update_mode (FM);
        }

      if (old_state != state () || send_update_signal_)
        {
          update_complete ();
          send_update_signal_ = false;
        }
      TRACE_CAT ("OmniRigTransceiver", "OmniRig params change: state after:" << state ());
    }
}

void OmniRigTransceiver::handle_custom_reply (int rig_number, QVariant const& command, QVariant const& reply)
{
  (void)command;
  (void)reply;

  if (rig_number_ == rig_number)
    {
      TRACE_CAT ("OmniRigTransceiver", "custom command" << command.toString ().toLocal8Bit ()
                 << "with reply" << reply.toString ().toLocal8Bit ()
                 << QString ("for rig %1").arg (rig_number).toLocal8Bit ());
      TRACE_CAT ("OmniRigTransceiver", "rig number:" << rig_number_ << ':' << state ());
    }
}

void OmniRigTransceiver::do_ptt (bool on)
{
  TRACE_CAT ("OmniRigTransceiver", on << state ());
  if (use_for_ptt_ && TransceiverFactory::PTT_method_CAT == ptt_type_)
    {
      TRACE_CAT ("OmniRigTransceiver", "set PTT");
      rig_->SetTx (on ? OmniRig::PM_TX : OmniRig::PM_RX);
    }
  else
    {
      if (port_)
        {
          if (TransceiverFactory::PTT_method_RTS == ptt_type_)
            {
              TRACE_CAT ("OmniRigTransceiver", "set RTS");
              port_->SetRts (on);
            }
          else      // "DTR"
            {
              TRACE_CAT ("OmniRigTransceiver", "set DTR");
              port_->SetDtr (on);
            }
        }
      else
        {
          TRACE_CAT ("OmniRigTransceiver", "set PTT using basic transceiver");
          Q_ASSERT (wrapped_);
          TransceiverState new_state {wrapped_->state ()};
          new_state.ptt (on);
          wrapped_->set (new_state, 0);
        }
    }
  update_PTT (on);
}

void OmniRigTransceiver::do_frequency (Frequency f, MODE m, bool /*no_ignore*/)
{
  TRACE_CAT ("OmniRigTransceiver", f << state ());
  if (UNK != m)
    {
      do_mode (m);
    }
  if (OmniRig::PM_FREQ & writable_params_)
    {
      rig_->SetFreq (f);
      update_rx_frequency (f);
    }
  else if (reversed_ && (OmniRig::PM_FREQB & writable_params_))
    {
      rig_->SetFreqB (f);
      update_rx_frequency (f);
    }
  else if (!reversed_ && (OmniRig::PM_FREQA & writable_params_))
    {
      rig_->SetFreqA (f);
      update_rx_frequency (f);
    }
  else
    {
      throw_qstring (tr ("OmniRig: don't know how to set rig frequency"));
    }
}

void OmniRigTransceiver::do_tx_frequency (Frequency tx, MODE m, bool /*no_ignore*/)
{
  TRACE_CAT ("OmniRigTransceiver", tx << state ());
  bool split {tx != 0};
  if (split)
    {
      if (UNK != m)
        {
          do_mode (m);
          if (OmniRig::PM_UNKNOWN == rig_->Vfo ())
            {
              if (writable_params_ & OmniRig::PM_VFOEQUAL)
                {
                  // nothing to do here because OmniRig will use VFO
                  // equalize to set the mode of the Tx VFO for us
                }
              else if ((writable_params_ & (OmniRig::PM_VFOA | OmniRig::PM_VFOB))
                   == (OmniRig::PM_VFOA | OmniRig::PM_VFOB))
                {
                  rig_->SetVfo (OmniRig::PM_VFOB);
                  do_mode (m);
                  rig_->SetVfo (OmniRig::PM_VFOA);
                }
              else if (writable_params_ & OmniRig::PM_VFOSWAP)
                {
                  rig_->SetVfo (OmniRig::PM_VFOSWAP);
                  do_mode (m);
                  rig_->SetVfo (OmniRig::PM_VFOSWAP);
                }
            }
        }
      TRACE_CAT ("OmniRigTransceiver", "set SPLIT mode on");
      rig_->SetSplitMode (state ().frequency (), tx);
      update_other_frequency (tx);
      update_split (true);
    }
  else
    {
      TRACE_CAT ("OmniRigTransceiver", "set SPLIT mode off");
      rig_->SetSimplexMode (state ().frequency ());
      update_split (false);
    }
  bool notify {false};
  if (readable_params_ & OmniRig::PM_FREQ || !(readable_params_ & (OmniRig::PM_FREQA | OmniRig::PM_FREQB)))
    {
      update_other_frequency (tx); // async updates won't return this
      // so just store it and hope
      // operator doesn't change the
      // "back" VFO on rig
      notify = true;
    }
  if (!((OmniRig::PM_VFOAB | OmniRig::PM_VFOBA | OmniRig::PM_SPLITON) & readable_params_))
    {
      TRACE_CAT ("OmniRigTransceiver", "setting SPLIT manually");
      update_split (split); // we can't read it so just set and
      // hope op doesn't change it
      notify = true;
    }
  if (notify)
    {
      update_complete ();
    }
}

void OmniRigTransceiver::do_mode (MODE mode)
{
  TRACE_CAT ("OmniRigTransceiver", mode << state ());
  // TODO: G4WJS OmniRig doesn't seem to have any capability of tracking/setting VFO B mode
  auto mapped = map_mode (mode);
  if (mapped & writable_params_)
    {
      rig_->SetMode (mapped);
      update_mode (mode);
    }
  else
    {
      offline ("OmniRig invalid mode");
    }
}
