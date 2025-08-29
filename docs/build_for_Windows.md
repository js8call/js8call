------------------------------------------------------------------------------
# Building JS8Call on Windows

This is a general overview with some slight variations for building Qt6-compliant JS8Call using JTSDK64-Tools (HamlibSDK) - see the detailed instructions for JTSDK64-Tools at Sourceforge:  https://hamlib-sdk.sourceforge.io/

**Prerequisites:**\
Windows 10 or 11

**Set up the JTSDK64-Tools build environment**
1) Install the JTSDK (Hamlib-SDK) 4.1.0 beta1 (the latest version from https://sourceforge.net/projects/hamlib-sdk as of this writing)
2) In the C:\JTSDK64-Tools\config directory, set the Hamlib file marker name to "hlnone", the qt file marker name to "qt6.6.3", and the source file marker name to "src-none"
3) Edit the Versions.ini file to change the following lines thusly:\
   `boostv=1.88.0`\
   `pulllatest=No`\
   `cpuusage=All`\
   `qt6v=6.6.3` (NOTE:  As of this writing, Qt6.6.3 is *required* for OmniRig functionality to work)
4) Run the JTSDK64-Setup script to install all the components per the instructions and install Qt6.6.3, then close the powershell window
5) Run the JTSDK64-Tools, and run `Deploy-Boost` to install Boost 1.88.0 (may take a while!), then close the powershell window
6) Open JTSDK64-Tools, run mingw64 and `menu` and build Dynamic Hamlib 4.6.4 (the default latest will install if you make no changes). To control the version installed, interrupt the Hamlib build process with a CTRL-C and open the Hamlib src directory (e.g., using Git Bash in Windows) and `git checkout 4.6.4` to set the version in source.  To see what versions of Hamlib are available, while in the hamlib directory, type `git branch -r` or `git tag` to get a list
7) To get JS8Call to package without errors, it might be necessary to obtain copies of the following files and place them in C:\Windows\SysWOW64\downlevel: api-ms-win-core-winrt-l1-1-0.dll and api-ms-win-core-winrt-string-l1-1-0.dl. (This MAY not be required any more, but I put this note here in case it might be.  Try without it first.)  I copied them from an old Win7x32 install CD, but they can also be found at several .dll download sites (be sure to scan them for viruses!!!)

**Building JS8Call**
1) Obtain the JS8Call source and place it in a folder named `wsjtx` in C:\JTSDK64-Tools\tmp, or open a Git Bash window in the C:\JTSDK64-Tools\tmp directory and issue a `git clone https://github.com/js8call/js8call.git wsjtx` command.  As with Hamlib, you can change the version of JS8Call 2.3.x you wish to build by opening a Git Bash command-line window in the wsjtx folder and issue the `git checkout <version>` command.  To see what versions are available in the repository, issue `git branch -r`
2) After Hamlib is built, close the powershell window, then re-open JTSDK64-Tools and build JS8Call with the `jtbuild package` command
3) Unless some requirements of the source change (e.g., a requirement to install a newer version of Qt or Boost), this build system can be used to simply do `jtbuild package` after the source has been updated
