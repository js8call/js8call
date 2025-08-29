# JS8Call
JS8Call is an experiment in combining the robustness of FT8 (a weak-signal mode by K1JT) with a messaging and network protocol layer for weak signal communication. The open source software is designed for connecting amateur radio operators who are operating under weak signal conditions and offers real-time keyboard-to-keyboard messaging, store-and-forward messaging, and automatic station announcements.

# Notice
JS8Call is a derivative of the WSJT-X application, restructured and redesigned for message passing using a custom FSK modulation called JS8. It is not supported by nor endorsed by the WSJT-X development group. While the WSJT-X group maintains copyright over the original work and code, JS8Call is a derivative work licensed under and in accordance with the terms of the GPLv3 license. The source code modifications are public and can be found in this repository: https://github.com/js8call/js8call .

* Read more on the original design inspiration here: https://github.com/jsherer/js8call

* For release announcements and discussion, join the JS8Call mailing list here: https://groups.io/g/js8call

* The latest official build and installers are available at https://github.com/js8call/js8call/releases
    * `win64.exe` for Windows
    * `.dmg` for Mac
    * `_amd64.deb` is the installer for Debian-based Linux systems such as Ubuntu, Mint, or Debian itself for regular PCs (not Raspi)
    * `x86_64.rpm` is the installer for Linux systems using the rpm package format, such as Fedora, for regular PCs.
    * `x86_64.AppImage` for Linux systems on regular PCs that face dependency problems with whichever of the previous two installers should be pertinent.
    * There is no installer for Raspi yet. For this and other platforms, or if all of our pre-build installers do not work on your particular system, you can try compiling from source code.

* Documentation is available here: https://docs.google.com/document/d/159S4wqMUVdMA7qBgaSWmU-iDI4C9wd4CuWnetN68O9U/edit?pli=1#heading=h.kfnyge37yfr

* Read below for recent updates to Qt6 and the removal of Fortran dependencies.

# Building JS8Call From Sourcecode
Instructions can be found in [docs](docs) in the source tree for building JS8Call on MacOS, Linux and Windows, as well as a contributor's guide.

# History
* July 6, 2017 - The initial idea of using a modification to the FT8 protocol to support long-form QSOs was developed by Jordan, KN4CRD, and submitted to the WSJT-X mailing list: https://sourceforge.net/p/wsjt/mailman/message/35931540/
* August 31, 2017 - Jordan, KN4CRD, did a little development and modified WSJT-X to support long-form QSOs using the existing FT8 protocol: https://sourceforge.net/p/wsjt/mailman/message/36020051/  He sent a video example to the WSJT-X group: https://widefido.wistia.com/medias/7bb1uq62ga
* January 8, 2018 - Jordan, KN4CRD, started working on the design of a long-form QSO application built on top of FT8 with a redesigned interface.
* February 9, 2018 - Jordan, KN4CRD, submitted question to the WSJT-X group to see if there was any interest in pursuing the idea: https://sourceforge.net/p/wsjt/mailman/message/36221549/
* February 10, 2018 - Jordan KN4CRD, Julian OH8STN, John N0JDS, and the Portable Digital QRP group did an experiment using FSQ. The idea of JS8Call, combining FT8, long-form QSOs, and FSQCall like features was born.
* February 11, 2018 - Jordan, KN4CRD, inquired about the idea of integrating long-form messages into WSJT-X: https://sourceforge.net/p/wsjt/mailman/message/36223372/
* February 12, 2018 - Joe Taylor, K1JT, wrote back: https://sourceforge.net/p/wsjt/mailman/message/36224507/ saying that “Please don't let my comment discourage you from proceeding as you wish, toward something new.”
* March 4, 2018 - Jordan, KN4CRD, published a design document for JS8Call: https://github.com/jsherer/js8call
* July 6, 2018 - Version 0.0.1 of JS8Call released to the development group
* July 15, 2018 - Version 0.1 released - a dozen testers
* July 21, 2018 - Version 0.2 released - 75 testers
* July 27, 2018 - Version 0.3 released - 150 testers
* August 12, 2018 - Version 0.4 released - (“leaked” on QRZ) - 500 testers
* September 2, 2018 - Version 0.5 released - 3000 testers
* September 14, 2018 - Version 0.6 released - 5000 testers
* October 8, 2018 - Version 0.7 released - 6000 testers, name changed to JS8 & JS8Call
* October 31, 2018 - Version 0.8 released - ~7000 testers
* November 15, 2018 - Version 0.9 released - ~7500 testers
* November 30, 2018 - Version 0.10 released - ~7800 testers
* December 18, 2018 - Version 0.11 released - ~8200 testers
* January 1, 2019 - Version 0.12 released - ~9000 testers
* January 23, 2019 - Version 0.13 released - ~9250 testers
* February 7, 2019 - Version 0.14 released - ~9600 testers
* February 21, 2019 - Version 1.0.0-RC1 released - ~10000 testers
* March 11, 2019 - Version 1.0.0-RC2 released - >10000 testers
* March 26, 2019 - Version 1.0.0-RC3 released - >11000 testers
* April 1, 2019 - Version 1.0.0 general availability - Public Release!
* June 6, 2019 - Version 1.1.0 general availability
* November 29, 2019 - Version 2.0.0 general availability - Fast and Turbo speeds introduced!
* December 22, 2019 - Version 2.1.0 general availability - Slow speed introduced!

# JS8Call Updates to v2.3.x
This is, in the flavor of `WSJTX-improved`, an 'improved' version of the original JS8Call, the source
code for which can be found at: https://bitbucket.org/widefido/js8call/src/js8call/

I am not the original author, and have no desire to create a fork, add new features, etc. My motivation
was to have a native version of JS8Call that would run on my Apple Silicon Mac, using a current version
of the Qt and Hamlib libraries. Along the way, I discovered and corrected a few bugs, and made some minor
visual improvements to the UI.

Anyway.....that's what this does; that's all this does. It's not intended to be anything but a vehicle
by which to provide my changes to the original author.

# Notable Changes
- Use of Fortran has been eliminated; everything that was previously implemented in Fortran has
  been ported to C++.
- The requirement for a separate decoder process and use of shared memory has been eliminated.
- Ported to Qt6, which changed the audio classes in a major way. Fortunately the wsjtx-improved
  team had been down this road already, and had dealt with most of the changes needed to the
  audio stuff.
- Vestiges of the original WSJTX codebase that were are longer relevant have been removed.
- The variable decode depth settings have been removed, as testing demonstrated that decodes
  beyond a depth of 2 were largely hope and dreams. The implementation now decodes at a fixed
  depth of 2 in all cases.
- Did a bit of work with alignment of data in the tables for better presentation.
- Improved the performance and appearance of the audio input VU meter.
- The attenuation slider was designed to look like an audio fader control, and it does a
  decent job of this in the `windows` style. However, the underlying `QSlider` control is not
  great in terms of styling consistency; it looks ok but not great in the `fusion` style, and
  quite bizarre in the `macos` style. I've attempted to rectify this via implementation of a
  custom-drawn `QSlider` implementation that consistently looks like a fader on any platform
  style, with the added advantage of always displaying the dB attenuation value.
- Adapted the waterfall scale drawing methodology to accommodate high-DPI displays.
- Hovering on the waterfall display now shows the frequency as a tooltip.
- The waterfall spectrum display has been substantially improved. This does mean that you'll
  have to re-select your preferred spectrum choice on first use, if your choice wasn't the
  default of 'Cumulative'. 'Linear Average' with a smoothing factor of 3 is particularly
  useful; either is in general a more helpful choice than the raw data shown by 'Current'.
- The waterfall display for Cumulative was displaying raw power, uncorrected to dB. Fixed.
- The waterfall display will now intelligently redraw on resize, rather than clearing.
- The 200Hz WSPR portion of the 30m band is now displayed more clearly, i.e., we label it
  as `WSPR`, and the sub-band indicator is located in a manner consistent with that of the
  JS8 sub-band indicators.
- Converted the boost library to an out-of-tree build.
- Updated the sqlite library.
- Updated the CRCpp library.
- Added the Eigen library.
- Fixed an issue where the message server and APRS client should have been moved to the network
  thread, but because they had parent objects, the moves failed.
- Ported the updated PSK reporter from the upstream WSJTX code, which allows for use of a TCP
  connection, and implements all of the advances in the upstream code, i.e., more efficient
  spotting to PSK Reporter, omission of redundant spots, and posting of spots is now spread
  more widely in time. As with WSJTX, temporarily, in support of the HamSCI Festivals of Eclipse
  Ionospheric Science, spots will be transmitted more frequently during solar eclipses; see
  https://www.hamsci.org/eclipse for details.
- Incorporated revised audio device selection methodology from the upstream WSJTX implementation:
  1. Where possible audio devices that disappear are not forgotten until the user selects
     another device, this should allow temporarily missing devices or forgetting to switch
     on devices before starting JS8Call to be handled more cleanly.
  2. Enumerating  audio devices is expensive and on Linux may take many seconds per device.
     To avoid lengthy blocking behavior until it is absolutely necessary, audio devices are
     not enumerated until one of the "Settings->Audio" device drop-down lists is opened.
     Elsewhere when devices must be discovered the enumeration stops as soon as the configured
     device is  discovered. A status bar message is posted when audio devices are being enumerated
     as a reminder that the UI may block while this is happening.
- Status messages couldn't be displayed in the status bar due to the progress widget taking up
  all available space; for the moment at least, it's restricted to be a defined size.
- Corrected a display resizing issue in the topmost section; seems to have affected only Linux
  systems, but in theory was broken on any platform.
- Updated the UDP reporting API to be multicast-aware.
- Separated display of distance and azimuth in the Calls table.
- Hovering over an azimuth in the Calls table will now display the closest cardinal compass direction.
- Azimuth and distance calculations will now use the 4th Maidenhead pair, i.e., the Extended field,
  if present.
- The Configuration dialog would allow invalid grid squares to be input; it will now allow only a
  valid square.
- Removed the undocumented and hidden `Audio/DisableInputResampling=true` configuration option.
- Windows, and only Windows, required a workaround to the Modulator as a result of changes in
  Qt 6.4, which presented as no sound being generated; OSX and Linux worked fine. The issue is
  described in https://bugreports.qt.io/browse/QTBUG-108672, and the workaround seems like a
  grody hack, but it's what WSJTX uses for the same issue, so we're in fine company here.

Qt6 by default will display using a platform-specific style. As a result, there will be some minor
display inconsistencies, e.g., progress bars, as displayed in the bottom of the main window, are
particularly platform-specific.

The earliest version of OSX that Qt6 supports is 11.0. It's set up to compile and link to run
on 11.0 or later, but I've only tested it on 14.6, 14.7, and 15.3.

Testing on Linux and Windows has been ably provided by Joe Counsil, K0OG, who does the bulk of the
grunt work while I largely just type things and drink coffee.

Allan Bazinet, W6BAZ