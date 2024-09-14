#
# Macros for processing ActiveX and COM controls with ActiveQt
#

if (WIN32)
  include (CMakeParseArguments)

  find_program (DUMPCPP_Executable dumpcpp.exe)

  # wrap_ax_server (outfiles inputfile ...)

  function (WRAP_AX_SERVER outfiles)
    set (options)
    set (oneValueArgs)
    set (multiValueArgs OPTIONS)
    
    cmake_parse_arguments (_WRAP_AX_SERVER "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})
    
    set (ax_server_files ${_WRAP_AX_SERVER_UNPARSED_ARGUMENTS})
    set (ax_server_options ${_WRAP_AX_SERVER_OPTIONS})

    foreach (it ${ax_server_files})
      get_filename_component (outfile ${it} NAME_WE)
      get_filename_component (infile ${it} ABSOLUTE)
      set (outfile ${CMAKE_CURRENT_BINARY_DIR}/${outfile})
      add_custom_command (
	OUTPUT ${outfile}.h ${outfile}.cpp
	COMMAND ${DUMPCPP_Executable}
	ARGS ${ax_server_options} -o "${outfile}" "${infile}"
	MAIN_DEPENDENCY ${infile} VERBATIM)
      list (APPEND ${outfiles} ${outfile}.cpp)
    endforeach()
    set(${outfiles} ${${outfiles}} PARENT_SCOPE)
  endfunction ()

endif (WIN32)