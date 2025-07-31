# JS8Call

JS8Call is an experiment in combining the robustness of FT8 (a weak-signal mode by K1JT) with a messaging and network protocol layer for weak signal communication. The open source software is designed for connecting amateur radio operators who are operating under weak signal conditions and offers real-time keyboard-to-keyboard messaging, store-and-forward messaging, and automatic station announcements. 

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

* Check [here](README-MacOS.md) for recent updates to Qt6, the removal of Fortran dependencies and how to build JS8Call on MacOS.

# Notice

JS8Call is a derivative of the WSJT-X application, restructured and redesigned for message passing using a custom FSK modulation called JS8. It is not supported by nor endorsed by the WSJT-X development group. While the WSJT-X group maintains copyright over the original work and code, JS8Call is a derivative work licensed under and in accordance with the terms of the GPLv3 license. The source code modifications are public and can be found in js8call branch of this repository: https://bitbucket.org/widefido/js8call/
