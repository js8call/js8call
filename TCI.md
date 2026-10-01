# TCI audio and CAT in JS8Call

JS8Call can talk to a TCI server (ExpertSDR2/3, Thetis, AetherSDR, and
anything else that speaks TCI 1.x/2.0) over a single WebSocket. That
socket carries both rig control and the modem audio, so you do not
need a virtual audio cable or a second CAT port.

## Settings

1. **File → Settings → Radio**
2. Rig: **TCI Client RX1** (or **TCI Client RX2** for the second receiver)
3. TCI Server: `localhost:50001`.
   ExpertSDR3's usual port is 50001. ExpertSDR2 is often 40001.
   A host:port or a full `ws://` / `wss://` URL both work.
4. PTT method: **CAT** (forced while a TCI rig is selected).
   Transmit audio is selected with `trx:<n>,true,tci`, which is what
   makes ExpertSDR use the TCI stream instead of the microphone.
5. Split operation: **Rig** if you use split. Fake It also works; the
   client sends `split_enable` and VFO B.
6. Mode: **Data/Pkt** so the rig is put in DIGU. USB is used if you
   leave the data mode on "None" / USB.

The sound-card input and output devices are not used for the modem
while a TCI rig is selected. Notification sounds still use the
notification device.

## What goes across the socket

Text commands, each ending in `;`:

- `vfo:<trx>,0,<hz>` and `vfo:<trx>,1,<hz>`
- `modulation:<trx>,digu` (also usb, lsb, digl, cw, am, nfm, wfm)
- `trx:<trx>,true,tci` / `trx:<trx>,false,tci`
- `split_enable:<trx>,true|false`
- `audio_samplerate:48000`
- `audio_stream_sample_type:3` (float32)
- `audio_stream_channels:2`
- `audio_start:<trx>` / `audio_stop:<trx>`
- `tx_enable:<trx>,true`
- `rx_enable:1,true` when RX2 is selected

Binary frames are the 64-byte little-endian TCI header used by
WSJT-X and ExpertSDR. `length` is the number of scalar samples.
Receive audio is converted to 48 kHz mono and written into the
existing decoder. Transmit audio is the modulator's 48 kHz PCM,
paced by `TX_CHRONO` when the server sends it. Until the first chrono
frame arrives, JS8Call sends a 20 ms frame of its own so the
modulator stays in real time and servers without a chrono clock
still transmit. After a chrono frame is seen, only chrono replies
are sent.

## Levels

The output-power slider is applied to the samples before they are
packed (it is not a sound-card volume control in this mode). Keep it
high enough that the radio develops power, and low enough that ALC
stays quiet. Receive level is whatever the radio puts on the TCI
stream; set RF gain on the rig so the JS8Call meter sits near 30 dB.

## Build

CMake already links Qt6::WebSockets. Configure and build the same way
you build stock JS8Call. The old `js8call.pro` file in the tree is
not the current build.
