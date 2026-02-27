cmake_minimum_required(VERSION 3.16)
project(azmap C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(OpenGL REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GLFW3 REQUIRED glfw3)
pkg_check_modules(GLEW REQUIRED glew)
pkg_check_modules(CURL REQUIRED libcurl)

# shapelib - try pkg-config first, fall back to find_library
pkg_check_modules(SHAPELIB shapelib)
if(NOT SHAPELIB_FOUND)
    find_library(SHAPELIB_LIBRARIES NAMES shp shapelib)
    find_path(SHAPELIB_INCLUDE_DIRS shapefil.h)
    if(NOT SHAPELIB_LIBRARIES)
        message(FATAL_ERROR "shapelib not found. Install with: sudo pacman -S shapelib")
    endif()
endif()

add_executable(azmap
    src/main.c
    src/config.c
    src/projection.c
    src/map_data.c
    src/renderer.c
    src/camera.c
    src/input.c
    src/text.c
    src/grid.c
    src/solar.c
    src/nightmesh.c
    src/ui.c
    src/qrz.c
)

target_include_directories(azmap PRIVATE
    ${GLFW3_INCLUDE_DIRS}
    ${GLEW_INCLUDE_DIRS}
    ${SHAPELIB_INCLUDE_DIRS}
    ${CURL_INCLUDE_DIRS}
    ${OPENGL_INCLUDE_DIR}
)

target_link_libraries(azmap PRIVATE
    ${GLFW3_LIBRARIES}
    ${GLEW_LIBRARIES}
    ${SHAPELIB_LIBRARIES}
    ${CURL_LIBRARIES}
    OpenGL::GL
    m
)

target_compile_options(azmap PRIVATE
    ${GLFW3_CFLAGS_OTHER}
    ${GLEW_CFLAGS_OTHER}
    -Wall -Wextra
)

# Copy shaders to build directory on every build
file(GLOB SHADER_FILES ${CMAKE_SOURCE_DIR}/shaders/*)
add_custom_target(copy_shaders ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/shaders ${CMAKE_BINARY_DIR}/shaders
    DEPENDS ${SHADER_FILES}
    COMMENT "Copying shaders to build directory"
)
add_dependencies(azmap copy_shaders)
