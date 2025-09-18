# Findlibusb
# ==========
#
# Find the usb library
#
# This will define the following variables::
#
#  Usb_FOUND	- True if the system has the usb library
#  Usb_VERSION	- The verion of the usb library which was found
#
# and the following imported targets::
#
#  Usb::Usb	- The libusb library
#

include (LibFindMacros)

if (WIN32)
  # Use path suffixes on MS Windows as we probably shouldn't
  # trust the PATH envvar. PATH will still be searched to find the
  # library as last resort.
  if (CMAKE_SIZEOF_VOID_P MATCHES "8")
    set (_library_options PATH_SUFFIXES MinGW64/dll MinGW64/static)
  else ()
    set (_library_options PATH_SUFFIXES MinGW32/dll MinGW32/static)
  endif ()
endif ()
libfind_pkg_detect (Usb usb-1.0
  FIND_PATH libusb.h PATH_SUFFIXES libusb-1.0
  FIND_LIBRARY usb-1.0 ${_library_options}
  )

libfind_process (Usb)

if (Usb_FOUND AND NOT TARGET Usb::Usb)
  add_library (Usb::Usb UNKNOWN IMPORTED)
  set_target_properties (Usb::Usb PROPERTIES
    IMPORTED_LOCATION "${Usb_LIBRARY}"
    INTERFACE_COMPILE_OPTIONS "${Usb_PKGCONF_CFLAGS_OTHER}"
    INTERFACE_INCLUDE_DIRECTORIES "${Usb_INCLUDE_DIRS}"
    INTERFACE_LINK_LIBRARIES "${Usb_LIBRARIES}"
    )
endif ()

mark_as_advanced (
  Usb_INCLUDE_DIR
  Usb_LIBRARY
  Usb_LIBRARIES
  )
