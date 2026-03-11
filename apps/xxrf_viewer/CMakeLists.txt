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


xxrf_apply_common(xxrf_viewer)
