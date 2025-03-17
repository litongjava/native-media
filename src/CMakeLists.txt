add_library(native_media SHARED native_mp3.c native_mp4.c)
# Link libraries
target_link_libraries(native_media
        ${JNI_LIBRARIES}
        ${AVCODEC_LIBRARY}
        ${AVFORMAT_LIBRARY}
        ${SWRESAMPLE_LIBRARY}
        ${AVUTIL_LIBRARY}
)