file(GLOB TEST_SOURCES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_LIST_DIR}/*_test.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/*_test.cc"
  "${CMAKE_CURRENT_LIST_DIR}/*_test.cxx"
)

if (NOT TEST_SOURCES)
  message(STATUS "No tests found in ${CMAKE_CURRENT_LIST_DIR}")
  return()
endif()

foreach(src ${TEST_SOURCES})
  get_filename_component(name ${src} NAME_WE)
  set(tgt "xxrf_test_${name}")

  add_executable(${tgt} ${src})
  target_link_libraries(${tgt} PRIVATE cxx_hackrf::cxx_hackrf)

  add_test(NAME ${tgt} COMMAND ${tgt})
endforeach()