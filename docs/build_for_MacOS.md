
------------------------------------------------------------------------------
# MacOS Prerequisites:
You will need Xcode and the Xcode commandline tools installed. Xcode can be downloaded from the Apple Store
for your Mac. For this example I used Xcode 16.2 on MacOS 15 Sequoia

Obtain cmake v4.03 from https://github.com/Kitware/CMake/releases/download/v4.0.3/cmake-4.0.3.tar.gz
Unpack the cmake source archive with Finder and follow the instructions in the README.rst to build
and install cmake on MacOS. This will be in Terminal by using cd to change into the cmake-4.0.3 directory
and running the following command:
```
./bootstrap && make && sudo make install
```
NOTE: you can also install cmake with homebrew. But recommended is to use the instructions above, which will install CMake ver 4.0.3, which has
been tested to be compatible with a JS8Call build.
------------------------------------------------------------------------------
# Getting the Libraries on MacOS
Below are the required libraries and tested versions for a JS8Call build on MacOS along with links to download them.

* [libusb-1.0.29](https://github.com/libusb/libusb/releases/download/v1.0.29/libusb-1.0.29.tar.bz2)
    
* [Hamlib-4.6.4](https://github.com/Hamlib/Hamlib/releases/download/4.6.4/hamlib-4.6.4.tar.gz)
    
* [fftw-3.3.10](https://fftw.org/fftw-3.3.10.tar.gz)
    
* [boost_1_88_0](https://archives.boost.io/release/1.88.0/source/boost_1_88_0.tar.gz)
    
* Qt-6.6.3 - this one is non-trivial and is a monster. Failed builds are common and will likely discourage you from trying a JS8Call
  build. Recommendations below......

*   There is two ways to obtain the libraries and frameworks to compile JS8Call; either build them yourself or fetch
    the pre-built libraries. Since building the libraries is non-trivial with Qt6 I recommend fetching the pre-built libraries
    from [here](https://github.com/Chris-AC9KH/js8lib/releases)

    If you wish to build the libraries yourself you can clone this [repository](https://github.com/Chris-AC9KH/js8lib)
    and follow the developer instructions. This uses an optimized version of Qt6.6.3 that is more likely to build on your platform. But be warned:
    while an M1 or later machine is really fast, if you are on an Intel machine it will take more than a half a day JUST to build Qt6.

*   In Terminal create the directory structure to build JS8Call with the following command.
    ```
    mkdir ~/development && mkdir ~/development/JS8Call
    ```
*   Download the library for your architecture with the above link and drag it to the project root `~development/JS8Call` in
    Finder. Double click on the archive to unpack it. It will create a folder called `js8lib`. Open Terminal and run the following command
    ```
    sudo mv ~/development/JS8Call/js8lib /usr/local/

------------------------------------------------------------------------------
# Building JS8Call on MacOS
We'll now fetch the JS8Call sourcecode with git:
```
cd ~/development/JS8Call && git clone https://github.com/js8call/js8call.git src
```
Your libraries are now in `/usr/local/js8lib` and the JS8Call source code is in `~/development/JS8Call/src`

NOTE: the main (dev) branch will be checked out by default. If you want to check out a different branch you can
use `cd ~/development/JS8Call/src` and then run `git switch release/2.3.1` to switch to and build the 2.3.1 branch, for instance. Or to list all the branches you can use `git branch -a`. It is now a simple matter of creating a build directory and run `cmake` to configure the build, followed by a `make install`. You can copy and paste this command in Terminal if you like.
```
cd ~/development/JS8Call/src && mkdir build && cd build \
&& cmake -DCMAKE_PREFIX_PATH=/usr/local/js8lib -DCMAKE_BUILD_TYPE=Release .. \
&& make install
```
If all goes well, you should end up with a `js8call.app` application in the build directory. Test by typing
`open ./js8call.app`. Once you're satisfied with the test results, copy js8call.app to /Applications. If you don't want to keep the js8lib
you can now delete it with
```
sudo rm -rf /usr/local/js8lib
```