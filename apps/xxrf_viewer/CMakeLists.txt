add_executable(xxrf_viewer
  ${CMAKE_CURRENT_LIST_DIR}/main.cpp
  ${CMAKE_CURRENT_LIST_DIR}/viewer_actions.cpp
  ${CMAKE_CURRENT_LIST_DIR}/viewer_ui.cpp
)

set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/third_party/imgui)

add_library(xxrf_imgui STATIC
  ${IMGUI_DIR}/imgui.cpp
  ${IMGUI_DIR}/imgui_draw.cpp
  ${IMGUI_DIR}/imgui_widgets.cpp
  ${IMGUI_DIR}/imgui_tables.cpp
  ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
  ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)

target_include_directories(xxrf_imgui PUBLIC
  ${IMGUI_DIR}
  ${IMGUI_DIR}/backends
)

target_compile_definitions(xxrf_imgui PUBLIC IMGUI_IMPL_OPENGL_LOADER_GLEW)

find_package(OpenGL REQUIRED)
find_package(glfw3 REQUIRED)
find_package(GLEW REQUIRED)

target_link_libraries(xxrf_imgui PUBLIC glfw OpenGL::GL GLEW::GLEW)


target_link_libraries(xxrf_viewer PRIVATE xxrf::aoa xxrf_imgui)
target_compile_definitions(xxrf_viewer PRIVATE XXRF_SOURCE_DIR="${CMAKE_SOURCE_DIR}")


xxrf_apply_common(xxrf_viewer)

if (WIN32)
  get_filename_component(XXRF_COMPILER_BIN_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
  string(JOIN "|" XXRF_VIEWER_RUNTIME_DIRS
    "${XXRF_COMPILER_BIN_DIR}"
    "${CMAKE_BINARY_DIR}/third_party/libhackrf/src"
    "$<TARGET_FILE_DIR:xxrf_viewer>"
  )

  set(XXRF_VIEWER_BUNDLE_ROOT "${CMAKE_BINARY_DIR}/dist")
  set(XXRF_VIEWER_BUNDLE_DIR "${XXRF_VIEWER_BUNDLE_ROOT}/xxrf_viewer-win64")
  set(XXRF_VIEWER_BUNDLE_README "${CMAKE_CURRENT_BINARY_DIR}/xxrf_viewer_bundle_README.txt")
  configure_file(
    ${CMAKE_CURRENT_LIST_DIR}/README.windows_bundle.txt.in
    ${XXRF_VIEWER_BUNDLE_README}
    @ONLY
  )

  add_custom_target(xxrf_bundle_viewer
    COMMAND ${CMAKE_COMMAND}
      -DXXRF_BUNDLE_DIR=${XXRF_VIEWER_BUNDLE_DIR}
      -DXXRF_VIEWER_EXE=$<TARGET_FILE:xxrf_viewer>
      -DXXRF_ASSETS_DIR=${CMAKE_SOURCE_DIR}/assets
      -DXXRF_RUNTIME_DIRS=${XXRF_VIEWER_RUNTIME_DIRS}
      -DXXRF_BUNDLE_README=${XXRF_VIEWER_BUNDLE_README}
      -P ${CMAKE_CURRENT_LIST_DIR}/bundle_viewer.cmake
    DEPENDS xxrf_viewer
    COMMENT "Bundling xxrf_viewer portable Windows distribution"
    VERBATIM
  )

  add_custom_target(xxrf_bundle_viewer_zip
    COMMAND ${CMAKE_COMMAND} -E rm -f ${XXRF_VIEWER_BUNDLE_ROOT}/xxrf_viewer-win64.zip
    COMMAND ${CMAKE_COMMAND} -E tar cf ${XXRF_VIEWER_BUNDLE_ROOT}/xxrf_viewer-win64.zip --format=zip xxrf_viewer-win64
    WORKING_DIRECTORY ${XXRF_VIEWER_BUNDLE_ROOT}
    DEPENDS xxrf_bundle_viewer
    COMMENT "Creating xxrf_viewer-win64.zip"
    VERBATIM
  )
endif()
