#include "Transceiver.hpp"

#include "moc_Transceiver.cpp"

#if !defined (QT_NO_DEBUG_STREAM)

ENUM_QDEBUG_OPS_IMPL (Transceiver, MODE);

QDebug operator << (QDebug d, Transceiver::TransceiverState const& s)
{
  d.nospace ()
    << "Transceiver::TransceiverState(online: " << (s.online_ ? "yes" : "no")
    << " Frequency {" << s.rx_frequency_ << "Hz, " << s.tx_frequency_ << "Hz} " << s.mode_
    << "; SPLIT: " << (Transceiver::TransceiverState::Split::on == s.split_ ? "on" : Transceiver::TransceiverState::Split::off == s.split_ ? "off" : "unknown")
    << "; PTT: " << (s.ptt_ ? "on" : "off")
    << ')';
  return d.space ();
}

#endif

ENUM_QDATASTREAM_OPS_IMPL (Transceiver, MODE);

ENUM_CONVERSION_OPS_IMPL (Transceiver, MODE);

bool operator != (Transceiver::TransceiverState const& lhs, Transceiver::TransceiverState const& rhs)
{
  return lhs.online_ != rhs.online_
    || lhs.rx_frequency_ != rhs.rx_frequency_
    || lhs.tx_frequency_ != rhs.tx_frequency_
    || lhs.mode_ != rhs.mode_
    || lhs.split_ != rhs.split_
    || lhs.ptt_ != rhs.ptt_;
}

bool operator == (Transceiver::TransceiverState const& lhs, Transceiver::TransceiverState const& rhs)
{
  return !(lhs != rhs);
}
