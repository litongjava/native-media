#include <stdio.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#include <libavutil/opt.h>

int main() {
  printf("Testing FFmpeg initialization...\n");

  // Print FFmpeg version info
  printf("libavcodec version: %s\n", av_version_info());
  printf("libavformat version: %d.%d.%d\n",
         LIBAVFORMAT_VERSION_MAJOR,
         LIBAVFORMAT_VERSION_MINOR,
         LIBAVFORMAT_VERSION_MICRO);

  // Test opening a format context
  AVFormatContext *fmt_ctx = NULL;
  printf("Creating format context...\n");

  fmt_ctx = avformat_alloc_context();
  if (!fmt_ctx) {
    printf("Failed to allocate format context\n");
    return -1;
  }

  printf("Format context created successfully\n");
  avformat_free_context(fmt_ctx);

  printf("Test completed successfully\n");
  return 0;
}