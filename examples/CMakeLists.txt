file(GLOB EXAMPLE_SOURCES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_LIST_DIR}/*.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/*.cc"
  "${CMAKE_CURRENT_LIST_DIR}/*.cxx"
)

if (NOT EXAMPLE_SOURCES)
  message(STATUS "No examples found in ${CMAKE_CURRENT_LIST_DIR}")
  return()
endif()

foreach(src ${EXAMPLE_SOURCES})
  get_filename_component(name ${src} NAME_WE)
  set(tgt "xxrf_example_${name}")

  add_executable(${tgt} ${src})
  target_link_libraries(${tgt} PRIVATE xxrf::xxrf)

  if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(${tgt} PRIVATE -Wall -Wextra)
  endif()
endforeach()