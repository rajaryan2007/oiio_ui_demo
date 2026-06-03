cmake_minimum_required(VERSION 3.6)
project(oiio_ui_demo)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(SDL3 REQUIRED)
find_package(OpenImageIO REQUIRED)
find_package(OpenGL REQUIRED)

set(VENDOR_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vendor")
set(IMGUI_DIR  "${VENDOR_DIR}/imgui")
set(GLAD_DIR   "${VENDOR_DIR}/glad")

set(IMGUI_SOURCES
  ${IMGUI_DIR}/imgui.cpp
  ${IMGUI_DIR}/imgui_draw.cpp
  ${IMGUI_DIR}/imgui_widgets.cpp
  ${IMGUI_DIR}/imgui_tables.cpp
  ${IMGUI_DIR}/backends/imgui_impl_sdl3.cpp
  ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)

add_executable(oiio_ui_demo
    src/main.cpp
    ${IMGUI_SOURCES}
    ${GLAD_DIR}/src/glad.c
)

target_include_directories(oiio_ui_demo PRIVATE
    src
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${GLAD_DIR}/include
    ${OPENIMAGEIO_INCLUDE_DIRS}
)

target_link_libraries(oiio_ui_demo PRIVATE
    SDL3::SDL3
    OpenImageIO::OpenImageIO
    OpenGL::GL
    ${CMAKE_DL_LIBS}
)






