file(GLOB APP_SOURCES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_LIST_DIR}/*.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/*.cc"
  "${CMAKE_CURRENT_LIST_DIR}/*.cxx"
)

if (NOT APP_SOURCES)
  message(STATUS "No apps found in ${CMAKE_CURRENT_LIST_DIR}")
  return()
endif()

foreach(src ${APP_SOURCES})
  get_filename_component(name ${src} NAME_WE)
  set(tgt "xxrf_app_${name}")

  add_executable(${tgt} ${src})
  target_link_libraries(${tgt} PRIVATE xxrf::xxrf Threads::Threads)

  if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(${tgt} PRIVATE -Wall -Wextra -Wpedantic)
  endif()
endforeach()