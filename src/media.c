#include <stdio.h>
#include <stdio.h>
#include <string.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include "native_mp3.h"
#include "native_media.h"

static void print_usage(const char *prog) {
  fprintf(stderr,
          "Usage:\n"
          "  %s test\n"
          "      Run FFmpeg initialization self-test.\n\n"
          "  %s to_mp3 <input.mp4> <output.mp3>\n"
          "      Convert input.mp4 to output.mp3 via convert_to_mp3().\n",
          prog, prog);
}

static void err2str(int errnum, char *buf, size_t buflen) {
  if (!buf || buflen == 0) return;
  av_strerror(errnum, buf, buflen);
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
    const char *in = argv[2];
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

  } else if (strcmp(argv[1], "to_mp3_for_silence") == 0) {
    // ------------------------------------------
    // to_mp3 子命令逻辑：调用 convert_to_mp3 接口
    // ------------------------------------------
    if (argc != 4) {
      print_usage(argv[0]);
      return 1;
    }
    const char *in = argv[2];
    const char *out = argv[3];

    printf("Converting '%s' -> '%s' …\n", in, out);
    char *msg = convert_to_mp3_for_silence(in, out, 0.72);
    if (!msg) {
      fprintf(stderr, "convert_to_mp3 returned NULL\n");
      return 1;
    }

    // 输出函数返回的结果（成功是输出路径，失败是错误信息）
    printf("%s\n", msg);
    free(msg);
    return 0;
  } else if (strcmp(argv[1], "save_last_frame") == 0) {
    if (argc < 4) {
      fprintf(stderr, "Usage: %s save_last_frame <input_video> <output_png>\n", argv[0]);
      return 2;
    }
    const char *in = argv[2];
    const char *out = argv[3];

    int rc = save_last_frame_c(in, out);
    if (rc == 0) {
      printf("OK: wrote last frame to %s\n", out);
    } else {
      char buf[256];
      err2str(rc, buf, sizeof(buf));
      fprintf(stderr, "FAIL (%d): %s\n", rc, buf);
    }
    return rc ? 1 : 0;

  } else if (strcmp(argv[1], "test_hls") == 0) {
    // ------------------------------------------
    // test_hls 子命令逻辑
    // ------------------------------------------
    if (argc < 3) {
      fprintf(stderr, "Usage: %s test_hls <output_directory>\n", argv[0]);
      return 1;
    }

    const char *out_dir = argv[2];
    char playlist_path[512];
    char ts_pattern[512];

    // 构造输出路径: output_dir/index.m3u8
    snprintf(playlist_path, sizeof(playlist_path), "%s/index.m3u8", out_dir);
    // 构造 TS 命名模板: output_dir/segment_%03d.ts
    snprintf(ts_pattern, sizeof(ts_pattern), "%s/segment_%%03d.ts", out_dir);

    printf("Starting HLS Session...\n");
    printf("Playlist: %s\n", playlist_path);
    printf("Pattern : %s\n", ts_pattern);

    // 1. 初始化会话 (StartNum=0, Duration=2s)
    HlsSession *session = init_hls_session(playlist_path, ts_pattern, 0, 2);
    if (!session) {
      fprintf(stderr, "Failed to init HLS session.\n");
      return 1;
    }

    // 2. 循环追加 10 个视频文件
    for (int i = 1; i <= 10; i++) {
      char input_file[256];
      // 假设视频都在 videos/ 目录下，命名为 Scene01.mp4, Scene02.mp4...
      snprintf(input_file, sizeof(input_file), "videos/Scene%02d.mp4", i);

      printf("Appending %s ... ", input_file);

      char *err = append_video_segment(session, input_file);
      if (err) {
        printf("[FAILED] %s\n", err);
        free(err);
        // 根据需求决定是否退出，这里继续尝试下一个
      } else {
        printf("[OK] Offset now: %.2f sec\n", (double) session->global_offset / 1000000.0);
      }
    }

    // 3. 结束会话
    printf("Finishing Session...\n");
    char *res = finish_hls_session(session, playlist_path);
    if (res) {
      printf("%s\n", res);
      free(res);
    }

    return 0;
  } else {
    // 未知子命令
    print_usage(argv[0]);
    return 1;
  }
}
