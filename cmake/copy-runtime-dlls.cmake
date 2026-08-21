if(NOT DEFINED FERRA_DLLS OR FERRA_DLLS STREQUAL "")
  return()
endif()

string(REPLACE "|" ";" FERRA_DLL_LIST "${FERRA_DLLS}")
foreach(FERRA_DLL IN LISTS FERRA_DLL_LIST)
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
      "${FERRA_DLL}" "${FERRA_DLL_DESTINATION}"
    COMMAND_ERROR_IS_FATAL ANY
  )
endforeach()
