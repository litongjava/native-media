cmake_minimum_required(VERSION 3.15)
project(native_media)

set(CMAKE_CXX_STANDARD 17)

# Find JNI
find_package(JNI REQUIRED)
include_directories(${JNI_INCLUDE_DIRS})

# Find FFmpeg components explicitly
find_path(AVCODEC_INCLUDE_DIR libavcodec/avcodec.h)
find_path(AVFORMAT_INCLUDE_DIR libavformat/avformat.h)
find_path(SWRESAMPLE_INCLUDE_DIR libswresample/swresample.h)
find_path(AVUTIL_INCLUDE_DIR libavutil/avutil.h)

find_library(AVCODEC_LIBRARY avcodec)
find_library(AVFORMAT_LIBRARY avformat)
find_library(SWRESAMPLE_LIBRARY swresample)
find_library(AVUTIL_LIBRARY avutil)

# Print paths for debugging
message(STATUS "AVCODEC_INCLUDE_DIR: ${AVCODEC_INCLUDE_DIR}")
message(STATUS "AVCODEC_LIBRARY: ${AVCODEC_LIBRARY}")
message(STATUS "AVFORMAT_INCLUDE_DIR: ${AVFORMAT_INCLUDE_DIR}")
message(STATUS "AVFORMAT_LIBRARY: ${AVFORMAT_LIBRARY}")
message(STATUS "SWRESAMPLE_INCLUDE_DIR: ${SWRESAMPLE_INCLUDE_DIR}")
message(STATUS "SWRESAMPLE_LIBRARY: ${SWRESAMPLE_LIBRARY}")
message(STATUS "AVUTIL_INCLUDE_DIR: ${AVUTIL_INCLUDE_DIR}")
message(STATUS "AVUTIL_LIBRARY: ${AVUTIL_LIBRARY}")

# 检测操作系统类型
if(WIN32)
    message(STATUS "Configuring for Windows")
    find_package(mp3lame CONFIG REQUIRED)
elseif(APPLE)
    message(STATUS "Configuring for macOS")
elseif(UNIX)
    message(STATUS "Configuring for Linux/UNIX")
endif()



# Include directories
include_directories(
        ${AVCODEC_INCLUDE_DIR}
        ${AVFORMAT_INCLUDE_DIR}
        ${SWRESAMPLE_INCLUDE_DIR}
        ${AVUTIL_INCLUDE_DIR}
        jni
)

# Add sourcesa
add_library(native_media SHARED src/native_mp3_split.c src/native_mp4_to_mp3.c src/native_media_support_format.c src/native_mp3.c
        src/native_media_av_convert.c src/native_media_av_split.c)

# Link libraries
target_link_libraries(native_media
        ${JNI_LIBRARIES}
        ${AVCODEC_LIBRARY}
        ${AVFORMAT_LIBRARY}
        ${SWRESAMPLE_LIBRARY}
        ${AVUTIL_LIBRARY})

if(WIN32)
    target_link_libraries(native_media mp3lame::mp3lame)
elseif(APPLE)

elseif(UNIX)
    target_link_libraries(native_media mp3lame)
endif()




# Add test executable
add_executable(mp4_test src/mp4_test.c src/native_mp4_to_mp3.c)
target_link_libraries(mp4_test
        ${AVCODEC_LIBRARY}
        ${AVFORMAT_LIBRARY}
        ${SWRESAMPLE_LIBRARY}
        ${AVUTIL_LIBRARY}
)