add_executable(xxrf_aoa_probe
  ${CMAKE_CURRENT_LIST_DIR}/main.cpp
)

target_link_libraries(xxrf_aoa_probe PRIVATE xxrf::aoa)

xxrf_apply_common(xxrf_aoa_probe)
