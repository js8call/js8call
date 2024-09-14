#
# Find the hamlib library
#
# This will define the following variables::
#
#  Hamlib_FOUND		- True if the system has the usb library
#  Hamlib_VERSION	- The verion of the usb library which was found
#
# and the following imported targets::
#
#  Hamlib::Hamlib	- The hamlib library
#

include (LibFindMacros)

libfind_pkg_detect (Hamlib hamlib
  FIND_PATH hamlib/rig.h PATH_SUFFIXES hamlib
  FIND_LIBRARY hamlib
  )

libfind_package (Hamlib Usb)

libfind_process (Hamlib)

if (NOT Hamlib_PKGCONF_FOUND)
  if (WIN32)
    set (Hamlib_LIBRARIES ${Hamlib_LIBRARIES};${Usb_LIBRARY};ws2_32)
  else ()
    set (Hamlib_LIBRARIES ${Hamlib_LIBRARIES};m;dl)
  endif ()
elseif (UNIX AND NOT APPLE)
  set (Hamlib_LIBRARIES ${Hamlib_PKGCONF_STATIC_LDFLAGS})
endif ()

# fix up extra link libraries for macOS as target_link_libraries gets
# it wrong for frameworks
unset (_next_is_framework)
unset (_Hamlib_EXTRA_LIBS)
foreach (_lib IN LISTS Hamlib_LIBRARIES Hamlib_PKGCONF_LDFLAGS)
  if (_next_is_framework)
    list (APPEND _Hamlib_EXTRA_LIBS "-framework ${_lib}")
    unset (_next_is_framework)
  elseif (${_lib} STREQUAL "-framework")
    set (_next_is_framework TRUE)
  else ()
    list (APPEND _Hamlib_EXTRA_LIBS ${_lib})
  endif ()
endforeach ()

if (Hamlib_FOUND AND NOT TARGET Hamlib::Hamlib)
  add_library (Hamlib::Hamlib UNKNOWN IMPORTED)
  set_target_properties (Hamlib::Hamlib PROPERTIES
    IMPORTED_LOCATION "${Hamlib_LIBRARY}"
    INTERFACE_COMPILE_OPTIONS "${Hamlib_PKGCONF_STATIC_OTHER}"
    INTERFACE_INCLUDE_DIRECTORIES "${Hamlib_INCLUDE_DIR}"
    INTERFACE_LINK_LIBRARIES "${_Hamlib_EXTRA_LIBS}"
    )
endif ()

mark_as_advanced (
  Hamlib_INCLUDE_DIR
  Hamlib_LIBRARY
  Hamlib_LIBRARIES
  )
