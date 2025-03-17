cmake_minimum_required(VERSION 3.27)
project(native_media)

set(CMAKE_CXX_STANDARD 17)

find_package(JNI REQUIRED)
message(STATUS "JNI_INCLUDE_DIRS = ${JNI_INCLUDE_DIRS}")
message(STATUS "JNI_LIBRARIES = ${JNI_LIBRARIES}")
include_directories(${JNI_INCLUDE_DIRS})

include_directories(jni)
add_subdirectory(src)
add_executable(main src/native_media_main.c src/native_media.c)
target_link_libraries(main ${JNI_LIBRARIES})