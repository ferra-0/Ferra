cmake_minimum_required(VERSION 3.21)

# CMake 4.x normalizes Windows paths before applying runtime-dependency
# include/exclude expressions. Opt in explicitly to avoid a warning for every
# system DLL discovered while creating the Windows package.
if(POLICY CMP0207)
  cmake_policy(SET CMP0207 NEW)
endif()

project(Ferra VERSION 1.16.0 LANGUAGES C CXX)

include(GNUInstallDirs)

option(FERRA_BUILD_EFERRA "Build the eFerra VM" ON)
option(FERRA_BUILD_AUDIO "Build the miniaudio native module" ON)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)
# Prefer a shared curl for portable Linux builds. FindCURL can otherwise pick
# /usr/local/lib/libcurl.a without the archive's transitive dependencies.
if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.28")
  set(CURL_USE_STATIC_LIBS OFF)
endif()
find_package(CURL REQUIRED)

add_library(ferra_curl INTERFACE)
set(FERRA_CURL_LINK_TARGET CURL::libcurl)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(_ferra_saved_library_suffixes "${CMAKE_FIND_LIBRARY_SUFFIXES}")
  set(CMAKE_FIND_LIBRARY_SUFFIXES ".so")
  find_library(FERRA_SHARED_CURL_LIBRARY NAMES curl)
  set(CMAKE_FIND_LIBRARY_SUFFIXES "${_ferra_saved_library_suffixes}")
  unset(_ferra_saved_library_suffixes)

  if(FERRA_SHARED_CURL_LIBRARY)
    add_library(ferra_shared_curl UNKNOWN IMPORTED)
    set_target_properties(ferra_shared_curl PROPERTIES
      IMPORTED_LOCATION "${FERRA_SHARED_CURL_LIBRARY}"
      INTERFACE_INCLUDE_DIRECTORIES "${CURL_INCLUDE_DIRS}")
    set(FERRA_CURL_LINK_TARGET ferra_shared_curl)
    message(STATUS
      "Ferra curl linkage: shared ${FERRA_SHARED_CURL_LIBRARY}")
  else()
    get_target_property(_ferra_curl_location CURL::libcurl IMPORTED_LOCATION)
    if(NOT _ferra_curl_location)
      get_target_property(_ferra_curl_location
        CURL::libcurl IMPORTED_LOCATION_RELEASE)
    endif()
    if(_ferra_curl_location MATCHES "\\.a$")
      find_package(PkgConfig QUIET)
      if(PkgConfig_FOUND)
        pkg_check_modules(FERRA_CURL_PC QUIET libcurl)
      endif()
      if(NOT FERRA_CURL_PC_FOUND OR
         NOT FERRA_CURL_PC_STATIC_LINK_LIBRARIES)
        message(FATAL_ERROR
          "Only static libcurl was found at ${_ferra_curl_location}, but its "
          "transitive link dependencies are unavailable. Install the shared "
          "libcurl development package or provide libcurl.pc for pkg-config.")
      endif()
      set(_ferra_curl_static_dependencies
        ${FERRA_CURL_PC_STATIC_LINK_LIBRARIES})
      list(REMOVE_ITEM _ferra_curl_static_dependencies curl libcurl)
      target_link_libraries(ferra_curl INTERFACE
        CURL::libcurl ${_ferra_curl_static_dependencies})
      set(_ferra_curl_link_configured TRUE)
      message(STATUS
        "Ferra curl linkage: static ${_ferra_curl_location} with pkg-config dependencies")
      unset(_ferra_curl_static_dependencies)
    endif()
    unset(_ferra_curl_location)
  endif()
endif()
if(NOT _ferra_curl_link_configured)
  target_link_libraries(ferra_curl INTERFACE ${FERRA_CURL_LINK_TARGET})
endif()
unset(_ferra_curl_link_configured)

add_executable(ferra
  ferralang/src/main.cpp
  ferralang/src/tokens.cpp
  ferralang/src/ast_parser.cpp
  ferralang/src/template_instantiator.cpp
  ferralang/src/llvm_emitter.cpp
  ferralang/src/file.cpp
)
target_include_directories(ferra PRIVATE ferralang/include)

function(ferra_native_object target source)
  add_library(${target} OBJECT ${source})
  target_include_directories(${target} PRIVATE cfe)
  set_target_properties(${target} PROPERTIES POSITION_INDEPENDENT_CODE ON)
endfunction()

ferra_native_object(cfe_json cfe/json.cpp)
ferra_native_object(cfe_fehttp cfe/fehttp.c)
ferra_native_object(cfe_native_bridge cfe/native_bridge.c)
ferra_native_object(cfe_pathdir cfe/pathdir.cpp)
ferra_native_object(cfe_thread cfe/thread.c)
ferra_native_object(cfe_process cfe/process.c)

target_link_libraries(cfe_fehttp PRIVATE ferra_curl Threads::Threads)
target_link_libraries(cfe_thread PRIVATE Threads::Threads)

set(FERRA_RUNTIME_OBJECTS
  $<TARGET_OBJECTS:cfe_json>
  $<TARGET_OBJECTS:cfe_fehttp>
  $<TARGET_OBJECTS:cfe_native_bridge>
  $<TARGET_OBJECTS:cfe_pathdir>
  $<TARGET_OBJECTS:cfe_thread>
  $<TARGET_OBJECTS:cfe_process>
)

add_library(ferra_runtime STATIC ${FERRA_RUNTIME_OBJECTS})
set_target_properties(ferra_runtime PROPERTIES
  OUTPUT_NAME ferra-runtime
  PREFIX "lib"
  SUFFIX ".a"
)
target_link_libraries(ferra_runtime PUBLIC ferra_curl Threads::Threads)
if(UNIX)
  target_link_libraries(ferra_runtime PUBLIC m)
endif()

if(FERRA_BUILD_AUDIO)
  ferra_native_object(cfe_miniaudio cfe/miniaudio.c)
  add_library(ferra_miniaudio STATIC $<TARGET_OBJECTS:cfe_miniaudio>)
  set_target_properties(ferra_miniaudio PROPERTIES
    OUTPUT_NAME ferra-miniaudio
    PREFIX "lib"
    SUFFIX ".a"
  )
  target_link_libraries(ferra_miniaudio PUBLIC Threads::Threads)
  if(CMAKE_SYSTEM_NAME MATCHES "^(Linux|Android)$")
    target_link_libraries(ferra_miniaudio PUBLIC dl m)
  elseif(APPLE)
    target_link_libraries(ferra_miniaudio PUBLIC
      "-framework CoreAudio"
      "-framework AudioToolbox"
      "-framework AudioUnit"
      "-framework CoreFoundation"
    )
  endif()
endif()

if(FERRA_BUILD_EFERRA)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set(FERRA_LLVM_COMPILER "${CMAKE_CXX_COMPILER}")
  else()
    find_program(FERRA_LLVM_COMPILER NAMES clang REQUIRED)
    message(STATUS
      "Ferra C/C++ compiler is ${CMAKE_CXX_COMPILER_ID}; using "
      "${FERRA_LLVM_COMPILER} only for generated eFerra LLVM IR")
  endif()

  file(GLOB EFERRA_SOURCES CONFIGURE_DEPENDS eferra/*.fe)
  file(GLOB FERRA_STDLIB_SOURCES CONFIGURE_DEPENDS fe/*.fe)
  set(EFERRA_IR "${CMAKE_CURRENT_BINARY_DIR}/generated/efe.ll")
  add_custom_command(
    OUTPUT "${EFERRA_IR}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory
      "${CMAKE_CURRENT_BINARY_DIR}/generated"
    COMMAND "${CMAKE_COMMAND}" -E env "FERRA_PATH=${CMAKE_CURRENT_SOURCE_DIR}"
      "$<TARGET_FILE:ferra>" "${CMAKE_CURRENT_SOURCE_DIR}/eferra/efe.fe"
      -o "${EFERRA_IR}"
    DEPENDS ferra ${EFERRA_SOURCES} ${FERRA_STDLIB_SOURCES}
    COMMENT "Compiling eFerra to LLVM IR"
    VERBATIM
  )

  set(EFERRA_OBJECT
    "${CMAKE_CURRENT_BINARY_DIR}/generated/efe${CMAKE_CXX_OUTPUT_EXTENSION}")
  add_custom_command(
    OUTPUT "${EFERRA_OBJECT}"
    COMMAND "${FERRA_LLVM_COMPILER}" -c -O3 -Wno-override-module
      "${EFERRA_IR}" -o "${EFERRA_OBJECT}"
    DEPENDS "${EFERRA_IR}"
    COMMENT "Compiling eFerra LLVM IR"
    VERBATIM
  )
  set_source_files_properties("${EFERRA_OBJECT}" PROPERTIES
    GENERATED TRUE EXTERNAL_OBJECT TRUE)
  add_executable(efe "${EFERRA_OBJECT}" ${FERRA_RUNTIME_OBJECTS})
  set_target_properties(efe PROPERTIES LINKER_LANGUAGE CXX)
  target_link_libraries(efe PRIVATE ferra_curl Threads::Threads)
  if(UNIX)
    target_link_libraries(efe PRIVATE m)
  endif()

  if(WIN32)
    add_custom_command(TARGET efe POST_BUILD
      COMMAND "${CMAKE_COMMAND}"
        "-DFERRA_DLLS=$<JOIN:$<TARGET_RUNTIME_DLLS:efe>,|>"
        "-DFERRA_DLL_DESTINATION=$<TARGET_FILE_DIR:efe>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/copy-runtime-dlls.cmake"
      VERBATIM
    )
  endif()
endif()

install(TARGETS ferra ferra_runtime
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}/ferra"
)
install(FILES $<TARGET_FILE:ferra_runtime>
  DESTINATION "${CMAKE_INSTALL_DATADIR}/ferra/cfe")
if(FERRA_BUILD_EFERRA)
  if(WIN32)
    # A Clang installation using the MinGW ABI links efe.exe against the GCC
    # runtime. file(GET_RUNTIME_DEPENDENCIES) does not search the compiler's
    # directory automatically, so make it available when CMake stages the ZIP.
    get_filename_component(FERRA_TOOLCHAIN_BIN_DIR
      "${CMAKE_CXX_COMPILER}" DIRECTORY)
    install(TARGETS efe
      RUNTIME_DEPENDENCIES
        DIRECTORIES "${FERRA_TOOLCHAIN_BIN_DIR}"
        PRE_EXCLUDE_REGEXES "api-ms-.*" "ext-ms-.*"
        POST_EXCLUDE_REGEXES ".*[Ss]ystem32[/\\\\].*\\.dll"
      RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
  else()
    install(TARGETS efe RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
  endif()
endif()
if(FERRA_BUILD_AUDIO)
  install(TARGETS ferra_miniaudio
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}/ferra")
  install(FILES $<TARGET_FILE:ferra_miniaudio>
    DESTINATION "${CMAKE_INSTALL_DATADIR}/ferra/cfe")
endif()

install(DIRECTORY fe DESTINATION "${CMAKE_INSTALL_DATADIR}/ferra")
install(DIRECTORY cfe DESTINATION "${CMAKE_INSTALL_DATADIR}/ferra"
  FILES_MATCHING PATTERN "*.c" PATTERN "*.cpp" PATTERN "*.h" PATTERN "*.hpp")
install(DIRECTORY eferra DESTINATION "${CMAKE_INSTALL_DATADIR}/ferra"
  PATTERN "__pycache__" EXCLUDE PATTERN "tests" EXCLUDE)
install(DIRECTORY ferralang/lsp DESTINATION "${CMAKE_INSTALL_DATADIR}/ferra/ferralang"
  PATTERN "__pycache__" EXCLUDE PATTERN "tests" EXCLUDE
  PATTERN "node_modules" EXCLUDE)
install(PROGRAMS lang.sh DESTINATION "${CMAKE_INSTALL_DATADIR}/ferra")
install(PROGRAMS packaging/vscode/make-vsix.py
  DESTINATION "${CMAKE_INSTALL_DATADIR}/ferra/packaging/vscode")
install(DIRECTORY icons DESTINATION "${CMAKE_INSTALL_DATADIR}/ferra")
# Bundle the VS Code language-client runtime so end-user installation stays
# offline and never needs npm. This dependency tree is tracked by the project.
install(DIRECTORY ferralang/tests/node_modules
  DESTINATION "${CMAKE_INSTALL_DATADIR}/ferra/ferralang/lsp/client"
  PATTERN ".bin" EXCLUDE
  PATTERN ".package-lock.json" EXCLUDE)
set(FERRA_IRON_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/ferralang/iron.efe")
if(WIN32)
  # A Windows checkout commonly stores source as CRLF. Ship Iron with stable LF
  # endings as an additional safeguard for older eFerra source loaders.
  set(FERRA_IRON_SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/generated/iron.efe")
  configure_file(ferralang/iron.efe "${FERRA_IRON_SCRIPT}"
    @ONLY NEWLINE_STYLE UNIX)
endif()
install(FILES "${FERRA_IRON_SCRIPT}"
  DESTINATION "${CMAKE_INSTALL_DATADIR}/ferra/ferralang")
install(FILES README.md DESTINATION "." OPTIONAL)

if(WIN32)
  install(PROGRAMS platforms/windows/iron.cmd
    DESTINATION "${CMAKE_INSTALL_BINDIR}")
  install(FILES platforms/windows/install-vscode.ps1
    DESTINATION "${CMAKE_INSTALL_DATADIR}/ferra/platforms/windows")
  install(FILES
    packaging/windows/install.ps1
    packaging/windows/uninstall.ps1
    DESTINATION ".")
else()
  install(PROGRAMS platforms/unix/iron
    DESTINATION "${CMAKE_INSTALL_BINDIR}")
  install(PROGRAMS
    packaging/unix/install.sh
    packaging/unix/uninstall.sh
    DESTINATION ".")
endif()

include(CTest)
if(BUILD_TESTING AND UNIX AND FERRA_BUILD_EFERRA)
  add_test(NAME ferra-regression
    COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/ferralang/tests/run.sh")
  set_tests_properties(ferra-regression PROPERTIES ENVIRONMENT
    "FERRA_COMPILER=$<TARGET_FILE:ferra>;FERRA_RUNTIME_LIBRARY=$<TARGET_FILE:ferra_runtime>")

  add_test(NAME eferra-regression
    COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/eferra/tests/run.sh")
  set_tests_properties(eferra-regression PROPERTIES ENVIRONMENT
    "FERRA_COMPILER=$<TARGET_FILE:ferra>;EFERRA_BIN=$<TARGET_FILE:efe>;FERRA_RUNTIME_LIBRARY=$<TARGET_FILE:ferra_runtime>")
endif()

string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" FERRA_PACKAGE_ARCH)
if(FERRA_PACKAGE_ARCH MATCHES "^(amd64|x86_64)$")
  set(FERRA_PACKAGE_ARCH "x86_64")
elseif(FERRA_PACKAGE_ARCH MATCHES "^(aarch64|arm64)$")
  set(FERRA_PACKAGE_ARCH "arm64")
endif()

set(FERRA_PACKAGE_PLATFORM "" CACHE STRING
  "Platform label used in the distributable archive name")
if(FERRA_PACKAGE_PLATFORM STREQUAL "")
  if(WIN32)
    set(FERRA_PACKAGE_PLATFORM "windows")
  elseif(APPLE)
    set(FERRA_PACKAGE_PLATFORM "macos")
  elseif(ANDROID OR CMAKE_SYSTEM_NAME STREQUAL "Android" OR
         DEFINED ENV{ANDROID_ROOT} OR DEFINED ENV{TERMUX_VERSION})
    set(FERRA_PACKAGE_PLATFORM "android")
  else()
    set(FERRA_PACKAGE_PLATFORM "linux")
  endif()
endif()
string(TOLOWER "${FERRA_PACKAGE_PLATFORM}" FERRA_PACKAGE_PLATFORM)
if(NOT FERRA_PACKAGE_PLATFORM MATCHES "^[a-z0-9_-]+$")
  message(FATAL_ERROR "Invalid FERRA_PACKAGE_PLATFORM: ${FERRA_PACKAGE_PLATFORM}")
endif()

set(CPACK_GENERATOR "ZIP")
set(CPACK_PACKAGE_NAME "Ferra")
set(CPACK_PACKAGE_VENDOR "Ferra")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_FILE_NAME
  "ferra-${PROJECT_VERSION}-${FERRA_PACKAGE_PLATFORM}-${FERRA_PACKAGE_ARCH}")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)
set(CPACK_PACKAGE_CHECKSUM "SHA256")
set(CPACK_VERBATIM_VARIABLES YES)
include(CPack)
