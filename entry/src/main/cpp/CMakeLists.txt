cmake_minimum_required(VERSION 3.5.0)
project(loafcore)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(LOAF_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

# note: re-run cmake when adding new source files (CMake GLOB is configure-time)     
file(GLOB LOAF_SOURCES
    ${LOAF_ROOT}/*.cpp
    ${LOAF_ROOT}/loaf/*.cpp
    ${LOAF_ROOT}/third_party/lua/*.c)

add_library(loafcore SHARED ${LOAF_SOURCES})

target_include_directories(loafcore PRIVATE
    ${LOAF_ROOT}
    ${LOAF_ROOT}/loaf
    ${LOAF_ROOT}/third_party
    ${LOAF_ROOT}/third_party/lua)

target_link_libraries(loafcore PRIVATE
    libace_napi.z.so
    libace_ndk.z.so
    libnative_window.so
    libEGL.so
    libGLESv3.so
    libhilog_ndk.z.so
    libnative_drawing.so
    libohaudio.so
    libavplayer.so
    libnative_image.so)
