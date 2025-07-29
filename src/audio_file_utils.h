#ifndef NATIVE_MEDIA_AUDIO_FILE_UTILS_H
#define NATIVE_MEDIA_AUDIO_FILE_UTILS_H

#include <libavformat/avformat.h>

int open_input_file_utf8(AVFormatContext **fmt_ctx, const char *filename);

int open_output_file_utf8(AVIOContext **pb, const char *filename);

#endif
