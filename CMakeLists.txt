cmake_minimum_required(VERSION 3.15)
project(native_media)

set(CMAKE_CXX_STANDARD 17)

# Find JNI
find_package(JNI REQUIRED)
include_directories(${JNI_INCLUDE_DIRS})

# Find FFmpeg components explicitly
find_path(AVCODEC_INCLUDE_DIR  libavcodec/avcodec.h)
find_path(AVFORMAT_INCLUDE_DIR libavformat/avformat.h)
find_path(SWRESAMPLE_INCLUDE_DIR libswresample/swresample.h)
find_path(SWSCALE_INCLUDE_DIR  libswscale/swscale.h)
find_path(AVUTIL_INCLUDE_DIR   libavutil/avutil.h)
find_path(AVFILTER_INCLUDE_DIR libavfilter/avfilter.h)

find_library(AVCODEC_LIBRARY   avcodec)
find_library(AVFORMAT_LIBRARY  avformat)
find_library(SWRESAMPLE_LIBRARY swresample)
find_library(SWSCALE_LIBRARY   swscale)
find_library(AVUTIL_LIBRARY    avutil)
find_library(AVFILTER_LIBRARY  avfilter)

# Print paths for debugging
message(STATUS "AVCODEC_INCLUDE_DIR: ${AVCODEC_INCLUDE_DIR}")
message(STATUS "AVCODEC_LIBRARY: ${AVCODEC_LIBRARY}")
message(STATUS "AVFORMAT_INCLUDE_DIR: ${AVFORMAT_INCLUDE_DIR}")
message(STATUS "AVFORMAT_LIBRARY: ${AVFORMAT_LIBRARY}")
message(STATUS "SWRESAMPLE_INCLUDE_DIR: ${SWRESAMPLE_INCLUDE_DIR}")
message(STATUS "SWRESAMPLE_LIBRARY: ${SWRESAMPLE_LIBRARY}")
message(STATUS "SWSCALE_INCLUDE_DIR: ${SWSCALE_INCLUDE_DIR}")
message(STATUS "SWSCALE_LIBRARY: ${SWSCALE_LIBRARY}")
message(STATUS "AVUTIL_INCLUDE_DIR: ${AVUTIL_INCLUDE_DIR}")
message(STATUS "AVUTIL_LIBRARY: ${AVUTIL_LIBRARY}")
message(STATUS "AVFILTER_INCLUDE_DIR: ${AVFILTER_INCLUDE_DIR}")
message(STATUS "AVFILTER_LIBRARY: ${AVFILTER_LIBRARY}")

# 检测操作系统类型
if (WIN32)
    message(STATUS "Configuring for Windows")
    find_package(mp3lame CONFIG REQUIRED)
elseif (APPLE)
    message(STATUS "Configuring for macOS")
elseif (UNIX)
    message(STATUS "Configuring for Linux/UNIX")
endif ()

# Include directories
include_directories(
        ${AVCODEC_INCLUDE_DIR}
        ${AVFORMAT_INCLUDE_DIR}
        ${SWRESAMPLE_INCLUDE_DIR}
        ${SWSCALE_INCLUDE_DIR}
        ${AVUTIL_INCLUDE_DIR}
        ${AVFILTER_INCLUDE_DIR}
        jni
)

# Add sources
add_library(native_media SHARED
        src/native_mp3_split.c
        src/native_mp4_to_mp3.c
        src/jni_utils.c
        src/native_media_support_format.c
        src/jni_native_mp3.c
        src/native_media_av_convert.c
        src/native_media_av_split.c
        src/native_video_to_hls.c
        src/jni_merge.c
        src/jni_video_length.c
        src/jni_video_watermark.c
        src/pure_video_to_hls.c
        src/pure_video_segment_to_hls.c
        src/jni_segment_mp4_to_hls.c
        src/native_mp3.c
        src/native_mp3_for_slience.c
        src/audio_file_utils.c
        src/jni_video_last_frame.c
        src/native_hls.c)

# Link libraries
# 注意：在 GNU ld 下库顺序很重要：把“依赖者在前，被依赖库在后”
target_link_libraries(native_media
        ${JNI_LIBRARIES}
        ${AVFORMAT_LIBRARY}
        ${AVCODEC_LIBRARY}
        ${SWRESAMPLE_LIBRARY}
        ${SWSCALE_LIBRARY}
        ${AVUTIL_LIBRARY}
        ${AVFILTER_LIBRARY}
        m
)

if (WIN32)
    target_link_libraries(native_media mp3lame::mp3lame)
elseif (APPLE)

elseif (UNIX)
    target_link_libraries(native_media mp3lame)
endif ()

# Add test executable
add_executable(media
        src/media.c
        src/native_mp4_to_mp3.c
        src/native_mp3.c
        src/native_mp3_for_slience.c
        src/audio_file_utils.c
        src/jni_video_last_frame.c
        src/pure_video_segment_to_hls.c
        src/native_hls.c)

target_link_libraries(media
        ${JNI_LIBRARIES}
        ${AVFORMAT_LIBRARY}
        ${AVCODEC_LIBRARY}
        ${SWRESAMPLE_LIBRARY}
        ${SWSCALE_LIBRARY}
        ${AVUTIL_LIBRARY}
        ${AVFILTER_LIBRARY}
        m
)

if (WIN32)
    target_link_libraries(media mp3lame::mp3lame)
elseif (APPLE)
    # 同上
elseif (UNIX)
    target_link_libraries(media mp3lame)
endif ()
