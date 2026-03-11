cmake_minimum_required(VERSION 3.24)

project(
  xxrf
  VERSION 0.1.0
  LANGUAGES C CXX
)


set(CMAKE_EXPORT_COMPILE_COMMANDS ON)


set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(GNUInstallDirs)

option(XXRF_BUILD_EXAMPLES "Build examples" ON)
option(XXRF_BUILD_APPS     "Build apps"     ON)
option(XXRF_BUILD_TESTS    "Build tests"    ON)

option(XXRF_USE_BUNDLED_LIBHACKRF "Use third_party/libhackrf" ON)

option(XXRF_ENABLE_WERROR      "Treat warnings as errors (our targets only)" OFF)
option(XXRF_ENABLE_SANITIZERS  "Enable ASan/UBSan for our targets only" OFF)
option(XXRF_ENABLE_LTO         "Enable LTO/IPO for our targets only" OFF)

find_package(Threads REQUIRED)


if (XXRF_USE_BUNDLED_LIBHACKRF)
  
  
  set(INSTALL_UDEV_RULES OFF CACHE BOOL "" FORCE)

  
  set(ENABLE_SHARED_LIB ON  CACHE BOOL "" FORCE)
  set(ENABLE_STATIC_LIB OFF CACHE BOOL "" FORCE)

  
  add_subdirectory(third_party/libhackrf EXCLUDE_FROM_ALL)
endif()


set(XXRF_LIBHACKRF_TARGET "")
if (TARGET HackRF::hackrf)
  set(XXRF_LIBHACKRF_TARGET HackRF::hackrf)
elseif (TARGET hackrf)
  set(XXRF_LIBHACKRF_TARGET hackrf)
else()
  message(FATAL_ERROR "libhackrf target not found. Expected HackRF::hackrf or hackrf.")
endif()


set(XXRF_LIBHACKRF_TARGET "${XXRF_LIBHACKRF_TARGET}" CACHE INTERNAL "")



add_subdirectory(src)

if (XXRF_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()

if (XXRF_BUILD_APPS)
  add_subdirectory(apps)
endif()

if (XXRF_BUILD_TESTS)
  include(CTest)
  enable_testing()
  add_subdirectory(tests)
endif()