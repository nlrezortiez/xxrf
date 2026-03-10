# Helper: apply common settings to either INTERFACE or normal libs.
function(xxrf_apply_common target)
  get_target_property(_type ${target} TYPE)

  if (_type STREQUAL "INTERFACE_LIBRARY")
    target_compile_features(${target} INTERFACE cxx_std_23)
    target_include_directories(${target} INTERFACE
      $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
      $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    )
  else()
    target_compile_features(${target} PUBLIC cxx_std_23)
    target_include_directories(${target} PUBLIC
      $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
      $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    )
  endif()

  # Warnings for our targets only
  if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    if (_type STREQUAL "INTERFACE_LIBRARY")
      target_compile_options(${target} INTERFACE
        -Wall -Wextra -Wpedantic -Wshadow
        -Wconversion -Wsign-conversion
        -Wnon-virtual-dtor -Wold-style-cast
      )
    else()
      target_compile_options(${target} PRIVATE
        -Wall -Wextra -Wpedantic -Wshadow
        -Wconversion -Wsign-conversion
        -Wnon-virtual-dtor -Wold-style-cast
      )
    endif()
  endif()

  if (XXRF_ENABLE_WERROR)
    if (_type STREQUAL "INTERFACE_LIBRARY")
      target_compile_options(${target} INTERFACE -Werror)
    else()
      target_compile_options(${target} PRIVATE -Werror)
    endif()
  endif()

  if (XXRF_ENABLE_LTO)
    set_property(TARGET ${target} PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
  endif()

  if (XXRF_ENABLE_SANITIZERS AND (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang"))
    set(_san "-fsanitize=address,undefined")
    if (_type STREQUAL "INTERFACE_LIBRARY")
      target_compile_options(${target} INTERFACE ${_san})
      target_link_options(${target} INTERFACE ${_san})
    else()
      target_compile_options(${target} PRIVATE ${_san})
      target_link_options(${target} PRIVATE ${_san})
    endif()
  endif()
endfunction()

# ------------------------------------------------------------------------------
# Core layer
# Recommended sources location:
#   src/core/*.cpp
# ------------------------------------------------------------------------------
file(GLOB_RECURSE XXRF_CORE_SOURCES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_LIST_DIR}/core/*.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/core/*.cc"
  "${CMAKE_CURRENT_LIST_DIR}/core/*.cxx"
)

if (XXRF_CORE_SOURCES)
  add_library(xxrf_core STATIC)
  target_sources(xxrf_core PRIVATE ${XXRF_CORE_SOURCES})
else()
  add_library(xxrf_core INTERFACE)
endif()

add_library(xxrf::core ALIAS xxrf_core)
xxrf_apply_common(xxrf_core)

# Link against libhackrf (bundled or external)
get_target_property(_xxrf_core_type xxrf_core TYPE)
if (_xxrf_core_type STREQUAL "INTERFACE_LIBRARY")
  target_link_libraries(xxrf_core INTERFACE ${XXRF_LIBHACKRF_TARGET})
else()
  target_link_libraries(xxrf_core PUBLIC ${XXRF_LIBHACKRF_TARGET})
endif()

# ------------------------------------------------------------------------------
# Stream layer (ring buffers, consumer thread, etc.)
# Recommended sources location:
#   src/stream/*.cpp
# ------------------------------------------------------------------------------
file(GLOB_RECURSE XXRF_STREAM_SOURCES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_LIST_DIR}/stream/*.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/stream/*.cc"
  "${CMAKE_CURRENT_LIST_DIR}/stream/*.cxx"
)

if (XXRF_STREAM_SOURCES)
  add_library(xxrf_stream STATIC)
  target_sources(xxrf_stream PRIVATE ${XXRF_STREAM_SOURCES})
else()
  add_library(xxrf_stream INTERFACE)
endif()

add_library(xxrf::stream ALIAS xxrf_stream)
xxrf_apply_common(xxrf_stream)

get_target_property(_xxrf_stream_type xxrf_stream TYPE)
if (_xxrf_stream_type STREQUAL "INTERFACE_LIBRARY")
  target_link_libraries(xxrf_stream INTERFACE xxrf::core Threads::Threads)
else()
  target_link_libraries(xxrf_stream PUBLIC xxrf::core Threads::Threads)
endif()

# ------------------------------------------------------------------------------
# Sync layer (dual-device start, trigger orchestration, alignment helpers)
#   src/sync/*.cpp
# ------------------------------------------------------------------------------
file(GLOB_RECURSE XXRF_SYNC_SOURCES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_LIST_DIR}/sync/*.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/sync/*.cc"
  "${CMAKE_CURRENT_LIST_DIR}/sync/*.cxx"
)

if (XXRF_SYNC_SOURCES)
  add_library(xxrf_sync STATIC)
  target_sources(xxrf_sync PRIVATE ${XXRF_SYNC_SOURCES})
else()
  add_library(xxrf_sync INTERFACE)
endif()

add_library(xxrf::sync ALIAS xxrf_sync)
xxrf_apply_common(xxrf_sync)

get_target_property(_xxrf_sync_type xxrf_sync TYPE)
if (_xxrf_sync_type STREQUAL "INTERFACE_LIBRARY")
  target_link_libraries(xxrf_sync INTERFACE xxrf::stream)
else()
  target_link_libraries(xxrf_sync PUBLIC xxrf::stream)
endif()

# --------------------------------------------------------------------------
# AoA layer
# src/aoa/*.cpp
# --------------------------------------------------------------------------
file(GLOB_RECURSE XXRF_AOA_SOURCES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_LIST_DIR}/aoa/*.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/aoa/*.cc"
  "${CMAKE_CURRENT_LIST_DIR}/aoa/*.cxx"
)

if (XXRF_AOA_SOURCES)
  add_library(xxrf_aoa STATIC)
  target_sources(xxrf_aoa PRIVATE ${XXRF_AOA_SOURCES})
else()
  add_library(xxrf_aoa INTERFACE)
endif()

add_library(xxrf::aoa ALIAS xxrf_aoa)
xxrf_apply_common(xxrf_aoa)

get_target_property(_ao xxrf_aoa TYPE)
if (NOT _ao STREQUAL "INTERFACE_LIBRARY")
  target_link_libraries(xxrf_aoa PUBLIC xxrf::sync)
else()
  target_link_libraries(xxrf_aoa INTERFACE xxrf::sync)
endif()

# ------------------------------------------------------------------------------
# Umbrella target: link one thing from apps/examples/tests
# ------------------------------------------------------------------------------
add_library(xxrf INTERFACE)
add_library(xxrf::xxrf ALIAS xxrf)

target_link_libraries(xxrf INTERFACE
  xxrf::core
  xxrf::stream
  xxrf::sync
  xxrf::aoa
)

xxrf_apply_common(xxrf)

