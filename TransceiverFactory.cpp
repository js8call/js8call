#include "TransceiverFactory.hpp"

#include <QMetaType>

#include "HamlibTransceiver.hpp"
#include "DXLabSuiteCommanderTransceiver.hpp"
#include "HRDTransceiver.hpp"
#include "EmulateSplitTransceiver.hpp"

#if defined (WIN32)
#include "OmniRigTransceiver.hpp"
#endif

#include "moc_TransceiverFactory.cpp"

// we use the hamlib "Hamlib Dummy" transceiver for non-CAT radios,
// this allows us to still use the hamlib PTT control features for a
// unified PTT control solution

char const * const TransceiverFactory::basic_transceiver_name_ = "None";

namespace
{
  enum				// supported non-hamlib radio interfaces
    {
      NonHamlibBaseId = 9899
      , CommanderId
      , HRDId
      , OmniRigOneId
      , OmniRigTwoId
    };
}

TransceiverFactory::TransceiverFactory ()
{
  HamlibTransceiver::register_transceivers (&transceivers_);
  DXLabSuiteCommanderTransceiver::register_transceivers (&transceivers_, CommanderId);
  HRDTransceiver::register_transceivers (&transceivers_, HRDId);

#if defined (WIN32)
  // OmniRig is ActiveX/COM server so only on Windows
  OmniRigTransceiver::register_transceivers (&transceivers_, OmniRigOneId, OmniRigTwoId);
#endif
}

TransceiverFactory::~TransceiverFactory ()
{
  HamlibTransceiver::unregister_transceivers ();
}

auto TransceiverFactory::supported_transceivers () const -> Transceivers const&
{
  return transceivers_;
}

auto TransceiverFactory::CAT_port_type (QString const& name) const -> Capabilities::PortType
{
  return supported_transceivers ()[name].port_type_;
}

bool TransceiverFactory::has_CAT_PTT (QString const& name) const
{
  return
    supported_transceivers ()[name].has_CAT_PTT_
    || supported_transceivers ()[name].model_number_ > NonHamlibBaseId;
}

bool TransceiverFactory::has_CAT_PTT_mic_data (QString const& name) const
{
  return supported_transceivers ()[name].has_CAT_PTT_mic_data_;
}

bool TransceiverFactory::has_CAT_indirect_serial_PTT (QString const& name) const
{
  return supported_transceivers ()[name].has_CAT_indirect_serial_PTT_;
}

bool TransceiverFactory::has_asynchronous_CAT (QString const& name) const
{
  return supported_transceivers ()[name].asynchronous_;
}

std::unique_ptr<Transceiver> TransceiverFactory::create (ParameterPack const& params, QThread * target_thread)
{
  std::unique_ptr<Transceiver> result;
  switch (supported_transceivers ()[params.rig_name].model_number_)
    {
    case CommanderId:
      {
        std::unique_ptr<TransceiverBase> basic_transceiver;
        if (PTT_method_CAT != params.ptt_type)
          {
            // we start with a dummy HamlibTransceiver object instance that can support direct PTT
            basic_transceiver.reset (new HamlibTransceiver {params.ptt_type, params.ptt_port});
            if (target_thread)
              {
                basic_transceiver.get ()->moveToThread (target_thread);
              }
          }

        // wrap the basic Transceiver object instance with a decorator object that talks to DX Lab Suite Commander
        result.reset (new DXLabSuiteCommanderTransceiver {std::move (basic_transceiver), params.network_port, PTT_method_CAT == params.ptt_type, params.poll_interval});
        if (target_thread)
          {
            result->moveToThread (target_thread);
          }
      }
      break;

    case HRDId:
      {
        std::unique_ptr<TransceiverBase> basic_transceiver;
        if (PTT_method_CAT != params.ptt_type)
          {
            // we start with a dummy HamlibTransceiver object instance that can support direct PTT
            basic_transceiver.reset (new HamlibTransceiver {params.ptt_type, params.ptt_port});
            if (target_thread)
              {
                basic_transceiver.get ()->moveToThread (target_thread);
              }
          }

        // wrap the basic Transceiver object instance with a decorator object that talks to ham Radio Deluxe
        result.reset (new HRDTransceiver {std::move (basic_transceiver), params.network_port, PTT_method_CAT == params.ptt_type, params.audio_source, params.poll_interval});
        if (target_thread)
          {
            result->moveToThread (target_thread);
          }
      }
      break;

#if defined (WIN32)
    case OmniRigOneId:
      {
        std::unique_ptr<TransceiverBase> basic_transceiver;
        if (PTT_method_CAT != params.ptt_type && "CAT" != params.ptt_port)
          {
            // we start with a dummy HamlibTransceiver object instance that can support direct PTT
            basic_transceiver.reset (new HamlibTransceiver {params.ptt_type, params.ptt_port});
            if (target_thread)
              {
                basic_transceiver.get ()->moveToThread (target_thread);
              }
          }

        // wrap the basic Transceiver object instance with a decorator object that talks to OmniRig rig one
        result.reset (new OmniRigTransceiver {std::move (basic_transceiver), OmniRigTransceiver::One, params.ptt_type, params.ptt_port});
        if (target_thread)
          {
            result->moveToThread (target_thread);
          }
      }
      break;

    case OmniRigTwoId:
      {
        std::unique_ptr<TransceiverBase> basic_transceiver;
        if (PTT_method_CAT != params.ptt_type && "CAT" != params.ptt_port)
          {
            // we start with a dummy HamlibTransceiver object instance that can support direct PTT
            basic_transceiver.reset (new HamlibTransceiver {params.ptt_type, params.ptt_port});
            if (target_thread)
              {
                basic_transceiver.get ()->moveToThread (target_thread);
              }
          }

        // wrap the basic Transceiver object instance with a decorator object that talks to OmniRig rig two
        result.reset (new OmniRigTransceiver {std::move (basic_transceiver), OmniRigTransceiver::Two, params.ptt_type, params.ptt_port});
        if (target_thread)
          {
            result->moveToThread (target_thread);
          }
      }
      break;
#endif

    default:
      result.reset (new HamlibTransceiver {supported_transceivers ()[params.rig_name].model_number_, params});
      if (target_thread)
        {
          result->moveToThread (target_thread);
        }
      break;
    }

  if (split_mode_emulate == params.split_mode)
    {
      // wrap the Transceiver object instance with a decorator that emulates split mode
      result.reset (new EmulateSplitTransceiver {std::move (result)});
      if (target_thread)
        {
          result->moveToThread (target_thread);
        }
    }

  return result;
}

#if !defined (QT_NO_DEBUG_STREAM)
ENUM_QDEBUG_OPS_IMPL (TransceiverFactory, DataBits);
ENUM_QDEBUG_OPS_IMPL (TransceiverFactory, StopBits);
ENUM_QDEBUG_OPS_IMPL (TransceiverFactory, Handshake);
ENUM_QDEBUG_OPS_IMPL (TransceiverFactory, PTTMethod);
ENUM_QDEBUG_OPS_IMPL (TransceiverFactory, TXAudioSource);
ENUM_QDEBUG_OPS_IMPL (TransceiverFactory, SplitMode);
#endif

ENUM_QDATASTREAM_OPS_IMPL (TransceiverFactory, DataBits);
ENUM_QDATASTREAM_OPS_IMPL (TransceiverFactory, StopBits);
ENUM_QDATASTREAM_OPS_IMPL (TransceiverFactory, Handshake);
ENUM_QDATASTREAM_OPS_IMPL (TransceiverFactory, PTTMethod);
ENUM_QDATASTREAM_OPS_IMPL (TransceiverFactory, TXAudioSource);
ENUM_QDATASTREAM_OPS_IMPL (TransceiverFactory, SplitMode);

ENUM_CONVERSION_OPS_IMPL (TransceiverFactory, DataBits);
ENUM_CONVERSION_OPS_IMPL (TransceiverFactory, StopBits);
ENUM_CONVERSION_OPS_IMPL (TransceiverFactory, Handshake);
ENUM_CONVERSION_OPS_IMPL (TransceiverFactory, PTTMethod);
ENUM_CONVERSION_OPS_IMPL (TransceiverFactory, TXAudioSource);
ENUM_CONVERSION_OPS_IMPL (TransceiverFactory, SplitMode);
