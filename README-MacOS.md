# JS8Call

This is, in the flavor of `WSJTX-improved`, an 'improved' version of the original JS8Call, the source
code for which can be found at: https://bitbucket.org/widefido/js8call/src/js8call/

I am not the original author, and have no desire to create a fork, add new features, etc. My motivation
was to have a native version of JS8Call that would run on my Apple Silicon Mac, using a current version
of the Qt and Hamlib libraries. Along the way, I discovered and corrected a few bugs, and made some minor
visual improvements to the UI.

Anyway.....that's what this does; that's all this does. It's not intended to be anything but a vehicle
by which to provide my changes to the original author.

Allan Bazinet, W6BAZ

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
------------------------------------------------------------------------------
# MacOS Prerequisites:
You will need Xcode and the Xcode commandline tools installed. Xcode can be downloaded from the Apple Store
for your Mac. For this example I used Xcode 16.2 on MacOS 15 Sequoia

Install Homebrew and install ninja with 'brew install ninja'
 
# Compiling JS8Call on MacOS
    Notes: the commands shown for Terminal can be copied and pasted if you wish.

 1. Obtain cmake ver 4.03 from https://github.com/Kitware/CMake/releases/download/v4.0.3/cmake-4.0.3.tar.gz
    Follow the instructions in the README.rst to build and install cmake on MacOS.

 2. Create a directory in which to build up the JS8Call dependencies; the name of this directory doesn't matter
    but must be used consistently, e.g., `~/development/js8libs`
    ~/development is your directory where the various libraries and build project source code will be stored.
    ~/development/js8libs is the library directory for building JS8Call and will be referenced with a --prefix
    flag in most build options. In Terminal do:
    ```
    mkdir ~/development && makdir ~/development/js8libs
    ```

 3. Obtain libusb ver 1.0.27 from https://github.com/libusb/libusb/releases/download/v1.0.27/libusb-1.0.27.tar.bz2
    Unpack the source distribution to the dependencies directory using Finder. In Terminal do:
    ```
    cd ~/development/libusb-1.0.27
    ./configure --prefix=~/development/js8libs
    make
    make install
    ```

 4. Obtain Hamlib ver 4.6.1 from https://github.com/Hamlib/Hamlib/releases/download/4.6.1/hamlib-4.6.1.tar.gz
    Unpack the source distribution to the dependencies directory using Finder: In Terminal do:
    ```
    cd ~/development/hamlib
    ./configure --prefix=~/development/js8libs
    make
    make install
    ```

 5. Obtain fftw ver 3.3.10 from https://fftw.org/fftw-3.3.10.tar.gz
    Unpack the source distribution to the dependencies directory using Finder. In Terminal do:
    ```
    cd ~/development/fftw-3.3.10
    ./configure CFLAGS=-mmacosx-version-min=11.0 \
                --prefix=~/development/js8libs \
                --enable-single \
                --enable-threads
    make
    make install
    ```
    Depending on the architecture in use, a non-trivial speedup can be performed by enabling
    SIMD instructions. For example, to do so on ARM architecture Macs, add `--enable-neon`.

    See https://www.fftw.org/fftw3_doc/Installation-on-Unix.html for further details on other architectures.

 6. Obtain boost 1.87.0 from https://github.com/boostorg/boost/releases/download/boost-1.87.0/boost-1.87.0-cmake.tar.gz
    Unpack the source distribution to the dependencies directory using Finder. In Terminal do:
    ```
    cd ~/development/boost-1.87.0
    ./bootstrap.sh --prefix=~/development/js8libs
    ./b2 -a -q
    ./b2 -a -q install
    ```

 7. Obtain, install and build Qt 6 from GitHub:
    ```
    cd ~/development
    git clone https://github.com/qt/qt5.git qt6
    cd qt6
    git switch 6.8.1
    ./init-repository
    ```

    The final command above will take awhile while git fetches the Qt submodules.
    When it completes, build Qt by creating a build directory:
    ```
    cd .. && mkdir qt6-build
    cd qt6-build
    ../qt6/configure -prefix ~/development/js8libs
    cmake --build . --parallel
    cmake --install .

    Note the dot in the above commands is important - do not omit it. The Qt6 build process will take a long time.
    Go get coffee, eat lunch, it still might not be done when you get back. Be patient.
    ```

 8. We're finally ready to build JS8Call. Note that after building the libraries they do not have to be rebuilt
    every time to build JS8Call. You can make modifications to the JS8Call source code for testing and use the same
    libraries you already built which are stored in ~/development/js8libs. Obtain the JS8Call sourcecode with git:
    ```
    cd ~/development && git clone https://github.com/js8call/js8call.git
    ```

    Create a build directory for JS8Call under this source tree, and run `cmake` to configure the build,
    followed by a `make install`
    ```
    cd js8call
    mkdir build && cd build
    cmake -DCMAKE_PREFIX_PATH=~/development/js8libs \
          -DCMAKE_BUILD_TYPE=Release ..
    make install
    ```
    If all goes well, you should end up with a `js8call.app` application in the build directory. Test using:
    ```
    open js8call.app
    ```
    Once you're satisfied with the test results, copy the `js8call` application to `/Applications`
