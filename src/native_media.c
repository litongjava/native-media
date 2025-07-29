#include <stdio.h>
#include <string.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include "native_mp3.h"

static void print_usage(const char *prog) {
  fprintf(stderr,
          "Usage:\n"
          "  %s test\n"
          "      Run FFmpeg initialization self-test.\n\n"
          "  %s to_mp3 <input.mp4> <output.mp3>\n"
          "      Convert input.mp4 to output.mp3 via convert_to_mp3().\n",
          prog, prog);
}

int main(int argc, char **argv) {
  if (argc < 2) {
    print_usage(argv[0]);
    return 1;
  }

  if (strcmp(argv[1], "test") == 0) {
    // -------------------
    // test 子命令逻辑：
    // -------------------
    printf("Testing FFmpeg initialization...\n");

    // 打印版本
    printf("libavcodec version: %s\n", av_version_info());
    printf("libavformat version: %d.%d.%d\n",
           LIBAVFORMAT_VERSION_MAJOR,
           LIBAVFORMAT_VERSION_MINOR,
           LIBAVFORMAT_VERSION_MICRO);

    // 创建并释放 AVFormatContext
    printf("Creating format context...\n");
    AVFormatContext *fmt_ctx = avformat_alloc_context();
    if (!fmt_ctx) {
      fprintf(stderr, "Failed to allocate format context\n");
      return -1;
    }
    printf("Format context created successfully\n");
    avformat_free_context(fmt_ctx);

    // 查找 libmp3lame 编码器
    const AVCodec *encoder = avcodec_find_encoder_by_name("libmp3lame");
    if (!encoder) {
      fprintf(stderr, "Error: Could not find libmp3lame encoder\n");
      return -1;
    }

    printf("Test completed successfully\n");
    return 0;

  } else if (strcmp(argv[1], "to_mp3") == 0) {
    // ------------------------------------------
    // to_mp3 子命令逻辑：调用 convert_to_mp3 接口
    // ------------------------------------------
    if (argc != 4) {
      print_usage(argv[0]);
      return 1;
    }
    const char *in  = argv[2];
    const char *out = argv[3];

    printf("Converting '%s' -> '%s' …\n", in, out);
    char *msg = convert_to_mp3(in, out);
    if (!msg) {
      fprintf(stderr, "convert_to_mp3 returned NULL\n");
      return 1;
    }

    // 输出函数返回的结果（成功是输出路径，失败是错误信息）
    printf("%s\n", msg);
    free(msg);
    return 0;

  } else {
    // 未知子命令
    print_usage(argv[0]);
    return 1;
  }
}
