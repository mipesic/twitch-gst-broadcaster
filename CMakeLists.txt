cmake_minimum_required(VERSION 3.14)

project(twitch_broadcaster C) #Project name

set(CMAKE_C_STANDARD 11)
find_package(PkgConfig)

#using pkg-config to getting Gstreamer
pkg_check_modules(GSTREAMER REQUIRED gstreamer-1.0)

#including GStreamer header files directory
include_directories(
        ${GLIB_INCLUDE_DIRS}
        ${GSTREAMER_INCLUDE_DIRS}
)

#linking GStreamer library directory
link_directories(
        ${GLIB_LIBRARY_DIRS}
        ${GSTREAMER_LIBRARY_DIRS}
)

add_executable(dyn_video_pipeline dyn_video_pipeline.c twitch_broadcaster.c)
target_link_libraries(dyn_video_pipeline ${GSTREAMER_LIBRARIES})