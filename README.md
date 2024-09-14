# JS8Call

This is, in the flavor of `WSJTX-improved`, an 'improved' version of the original JS8Call, the source
code for which can be found at: https://bitbucket.org/widefido/js8call/src/js8call/

I am not the original author, and have no desire to create a fork, add new features, etc. My motivation
was to have a native version of JS8Call that would run on my Apple Silicon Mac, using a current version
of the Qt and Hamlib libraries. Along the way, I discovered and corrected a few bugs, and made some minor
visual improvements to the UI.

I did stumble over a very signficant issue in the way that the underlying WSJTX code performed Fortran
string passing; this bug affects code compiled with GNU Fortran >= 8.0 and a non-GNU C++ compiler, such
as `clang`. This bug exhibits as seemingly random behavior, often things like inexplicable crashes in
azimuth and distance calculation. The fix has been provided to the WSJTX team, and it's present in
WSJTX-improved starting in the test version of August 29th, 2024.

Anyway.....that's what this does; that's all this does. It's not intended to be anything but a vehicle
by which to provide my changes to the original author.

Allan Bazinet, W6BAZ

# Notable Changes

- Fixed the aforementioned string passing bug; crashes from this were typically random, but it'd
  usually abort with a segmentation fault in azdist(), sometimes in genjs8().
- The subtractjs8() routine used a common block for large, parameterized arrays. I'd see crashes
  here frequently; it's been a long time since I did any serious development in Fortran, but if
  memory serves, this is a dark corner where in terms of how large that common block is, first
  one in wins, and you hope that it's the larger of the parameterized invocations. If instead,
  the smaller one is dominant, then you'll either trash memory after the common block or crash
  with a segfault. Changed this to use allocations instead of a common block, which seems like
  a solid solution, but again, been a long time since I did this sort of thing in anger.
- Fixed a memory leak present when a custom push to talk command was configured. I haven't gone
  looking for memory leaks with the analysis tools; that one just happened to stand out.
- Fixed a buffer overflow in the code that prints the last transmit.
- Ported to Qt6, which changed the audio classes in a major way. Fortunately the wsjtx-improved
  team had been down this road already, and had dealt with most of the changes needed to the
  audio stuff.
- Incorporated a number of fixes from the upstream WSJTX Fortran library. Key among these was a
  change to how VHF contest mode was handled in some of the common routines; in short, they made
  things a lot simpler, which eliminated the need for passing a lot of dummy parameters just to
  keep them happy.
- Eliminated a large number of unused dummy parameters in the JS8 decoder; warning spam from the
  Fortran compiler was a bit distracting, and hid actual issues, such as uninitialized variables
  in the optimized code.
- Did a bit of work with alignment of data in the tables for better presentation.
- The audio input VU meter looked off to me, as if the scale was on the wrong side; flipped it to
  be next to the level and peak hold, which looks more normal to me. Perhaps just a taste thing;
  easy enough to change it back if necessary. The peak hold indicator was very pixelated on a
  high-DPI display, so it's now antialiased. Fixed the scale to match the VU bar location, and
  addressed the fact that this widget would both update when if was not necessary to do so, would
  fail to update when it was necessary to do so, and would leak its children. All these are also
  present in the original WSJTX code; I need to make them aware, note to self.
- The audio input VU meter was set up such that the 'good' range was 15 dB from the bottom,
  but only 5 dB from the top. I've changed it such that it's 15 dB from both ends, which moves
  the 30 dB tuning spot to a major scale tick.
- Changed the waterfall scale drawing methodology slightly to avoid the scale font looking
  pixelated on high-DPI displays. Fonts will still be pixelated in the waterfall display, but
  it's arguably an effect there, like a Tektronix scope. The plot drawing code uses a number of
  intermediate pixmaps, so dealing with the font in the waterfall is complicated; it'd be nice
  to move this to the GL approach taken by SDRangel.
- Hovering on the waterfall display now shows the frequency as a tooltip.
- The waterfall display drawing hot loop, in particular, the spectrum display, was horribly
  inefficient and practically incomprehensible. I suspect this arose from originally having
  only a couple of types of spectrum display in the WSJTX code, so a boolean was used to
  differentiate them. They then added more types, and more types, additional booleans each
  time, and wow, figuring out what `y` means in the spectrum drawing code becomes a voyage
  of discovery every time through the loop. To address this horror, moved the `WFPalette`
  code into a new `WF` namespace as `WF::Palette`, and added a `WF::Spectrum` class enum to
  differentiate the spectrum types, as they're all mutually exclusive, so, yeah, we don't
  need umpteen boolean tests to know what we're drawing now. More can be done here, but
  this stops the bleeding for the moment. This does mean that you'll have to re-select your
  preferred spectrum choice on first use, if your choice wasn't the default of 'Cumulative',
  though, seriously, Cumulative is *The One*. Just sayin'.
- Converted the boost library to an out-of-tree build.
- Updated the sqlite and qcustomplot libraries. I don't think that JS8Call actually uses the
  qcustomplot library; it's just leftovers that could be gutted out. Again, note to self.
- Updated the Hamlib library to the current 4.6 snapshot, which provides support for many radios
  not previously supported, e.g., the 705.
- Updated the Fortran code generation to use 2018 semantics, i.e., `-frecursive`, when dealing
  with large arrays that exceed available stack space, i.e., use allocation, rather than a hidden
  static block, as this tends to be less surprising behavior in modern usage on systems where we
  have memory to burn.
- Fixed an issue where the message server and APRS client should have been moved to the network
  thread, but because they had parent objects, the moves failed. Other than a minor thread
  affinity change required to the APRS client's timer, all the proper setup was already in place
  for the moves, i.e., they were connected to be deleted when the network thread ended, they just
  happened to have parents, and so the moves didn't actually work, resulting in these components
  remaining on the originating thread.
- Ported the updated PSK reporter from the upstream WSJTX code, which allows for use of a TCP
  connection, and implements all of the advances in the upstream code, i.e., more efficient
  spotting to PSK Reporter, omission of redundant spots, and posting of spots is now spread
  more widely in time. As with WSJTX, temporarily, in support of the HamSCI Festivals of Eclipse
  Ionospheric Science, spots will be transmitted more frequently during solar eclipses; see
  https://www.hamsci.org/eclipse for details.
- The DriftingDateTime class was a completely static class, masquerading as a namespace. Since
  any minimum required compiler is now namespace-aware, converted it to a namespace. Added a
  currentSecsSinceEpoch() fuction to match that of QDateTime.
- Incorporated revised audio device selection methodology from the upstream WSJTX implementation:
  1. Where possible audio devices that disappear are not forgotten until the user selects
     another device, this should allow temporarily missing devices or forgetting to switch
     on devices before starting JS8Call to be handled more cleanly.
  2. Enumerating  audio devices is expensive and on Linux may take many seconds per device.
     To avoid lengthy blocking behaviour until it is absolutely necessary, audio devices are
     not enumerated until one of the "Settings->Audio" device drop-down lists is opened.
     Elsewhere when devices must be discovered the enumeration stops as soon as the configured
     device is  discovered. A status bar message is posted when audio devices are being enumerated
     as a reminder that the UI may block while this is happening.
- Status messages couldn't be displayed in the status bar due to the progress widget taking up
  all available space; for the moment at least, it's restricted to be a defined size.
- Removed an old workaround on OSX for sub-menu display issues that do not seem to be relevant
  to Qt6.
- The UI was hardcoding use of `MS Shell Dlg 2` font  in a few places, principally in the dial
  offset display and the clock. That font is now as one with the dust of history, even on Windows;
  it was taking the startup about 200 milliseconds to figure out suitable replacements, and that’s
  time we can’t get back. Given that it was a sans-serif font very similar to Tahoma or Arial,
  which are the Qt system defaults on just about any platform, I’ve just told it to use the default
  font instead.
- Windows, and only Windows, required a workaround to the Modulator as a result of changes in
  Qt 6.4, which presented as no sound being generated; OSX and Linux worked fine. The issue is
  described in https://bugreports.qt.io/browse/QTBUG-108672, and the workaround seems like a
  grody hack, but it's what WSJTX uses for the same issue, so we're in fine company here.

While Qt6 by default will display using a platform-specific style, I've not yet done much work to
deal with changes required there (e.g., platform-specific stylesheet changes, where custom styles
are in use), so for the moment it continues to default to the previous Windows look and feel. One
can override this at runtime with the usual command line parameter to try an alternate style, e.g.,
```
open ./js8call.app --args -style fusion
open ./js8call.app --args -style macos
```

The earliest version of OSX that Qt6 supports is 11.0. It's set up to compile and link to run
on 11.0 or later, but I've only tested it on 14.6.

Testing on Linux and Windows has been ably provided by Joe Counsil, K0OG, who does the bulk of the
grunt work while I largely just, you know, type things and drink coffee.

# Compiling on OSX

 1. Obtain the current version of GNU Fortran, as of this writing 14.2, from https://github.com/fxcoudert/gfortran-for-macOS,
    and install it; you'll end up with an installation in `/usr/local/gfortran`.

 2. Obtain the current version of CMake from https://cmake.org/download/. Both source and binary
    distributions are available; I usually obtain the source distribution and compile, but whatever
    floats your boat is fine.

 3. Create a directory in which to build up the dependencies; the name of this directory doesn't matter,
    but must be used consistently, e.g., `/Users/<username>/Development/js8libs`. Everything we require
    as a dependency will be installed to this path. For purposes of clarity in this document, we'll use:
    ```
    /Users/alb/Development/js8libs
    ```
    Modify as appropriate for your own username and directory structure.

 4. Obtain the current release of libusb, presently version 1.0.27, from https://sourceforge.net/projects/libusb/.
    Unpack the source distribution and install it to the dependencies directory:
    ```
    ./configure --prefix=/Users/alb/Development/js8libs
    make
    make install
    ```

 5. Obtain the current daily snapshot of the Hamlib code from https://n0nb.users.sourceforge.net/; at
    present, this will be some master build of 4.6. Unpack the source distribution and install it to the
    dependencies directory:
    ```
    ./configure --prefix=/Users/alb/Development/js8libs
    make
    make install
    ```

 6. Obtain the current release of fftw, presently version 3.3.10, from https://www.fftw.org/. Unpack the
    source distribution and install it to the dependencies directory:
    ```
    ./configure CFLAGS=-mmacosx-version-min=11.0 \
                --prefix=/Users/alb/Development/js8libs \
                --enable-single \
                --enable-threads
    make
    make install
    ```

 7. Obtain the current release of boost, presently 1.86.0, from https://www.boost.org/. Unpack the source
    distribution and install it to the dependencies directory:
    ```
    ./bootstrap.sh --prefix=/Users/alb/Development/js8libs
    ./b2 -a -q
    ./b2 -a -q install
    ```

 8. Obtain and install Qt 6, using the documentation here: https://doc.qt.io/qt-6/macos-building.html.
    When configuring, use the usual `-prefix` option to install the built products into the dependencies
    directory.

 9. Optionally, obtain and install Qt Creator: https://wiki.qt.io/Building_Qt_Creator_from_Git. By
    now, you should be familiar with use of the dependencies directory, so we'll leave that as an
    exercise for the student. You got this. 

    While not strictly necessary, Qt Creator certainly makes debugging relatively simple, so I'd
    go for it, frankly.

10. Create a build directory, typically under this source tree, and run `cmake` to configure the build,
    followed by a `make install`.
    ```
    mkdir build
    cd build
    cmake -DCMAKE_PREFIX_PATH=/Users/alb/Development/js8libs \
           -DCMAKE_BUILD_TYPE=Release ..
    make install
    ```
    There should be a small handful of Fortran warnings; it's practically impossible to get rid of all
    of them; some are false positives, and some just reflect the fact that it's a language with a lot
    of history behind it, and certain things, like using the same array for both in and out intents,
    will tend to upset it, even if that usage is not a problem. If all goes well, you should end up
    with a `js8call` application in the build directory. Test using:
    ```
    open ./js8call.app
    ```
    Once you're satisfied with the test results, copy the `js8call` application to `/Applications`.
