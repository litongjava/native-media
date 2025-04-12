#include "native_media.h"
#include <stdio.h>
#include <stdlib.h>

// 使用方法：
//   ./test_native_media <playlist.m3u8> <ts_pattern> <start_number> <segment_duration> <input_mp4_1> [<input_mp4_2> ...]
int main(int argc, char *argv[]) {
  if (argc < 6) {
    fprintf(stderr,
            "Usage: %s <playlist.m3u8> <ts_pattern> <start_number> <segment_duration> <input_mp4_1> [input_mp4_2 ...]\n",
            argv[0]);
    return 1;
  }
  const char *playlist = argv[1];
  const char *ts_pattern = argv[2];
  int start_number = atoi(argv[3]);
  int seg_duration = atoi(argv[4]);

  HlsSession *session = init_persistent_hls(playlist, ts_pattern, start_number, seg_duration);
  if (!session) {
    fprintf(stderr, "Failed to initialize HLS session\n");
    return 1;
  }
  int ret = 0;
  for (int i = 5; i < argc; i++) {
    printf("Appending segment: %s\n", argv[i]);
    ret = append_video_segment_to_hls(session, argv[i]);
    if (ret < 0) {
      fprintf(stderr, "Error appending segment: %s\n", argv[i]);
      break;
    }
  }
  ret = finish_prersistent_hls(session);
  if (ret < 0) {
    fprintf(stderr, "Error finishing HLS session\n");
    return ret;
  }
  printf("HLS session completed successfully.\n");
  return 0;
}
