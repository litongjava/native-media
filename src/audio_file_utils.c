#include <stdlib.h>
// FFmpeg 头文件
#include <libavformat/avformat.h>
#include <libavutil/opt.h>

#include <libavutil/samplefmt.h>


#ifdef _WIN32

#include <stringapiset.h>

#endif

#include "audio_file_utils.h"

// Helper function to open file with UTF-8 path on Windows
int open_input_file_utf8(AVFormatContext **fmt_ctx, const char *filename) {
#ifdef _WIN32
  int wlen = MultiByteToWideChar(CP_UTF8, 0, filename, -1, NULL, 0);
  if (wlen <= 0) return AVERROR(EINVAL);
  wchar_t *wfilename = malloc(wlen * sizeof(wchar_t));
  if (!wfilename) return AVERROR(ENOMEM);
  MultiByteToWideChar(CP_UTF8, 0, filename, -1, wfilename, wlen);
  int len = WideCharToMultiByte(CP_UTF8, 0, wfilename, -1, NULL, 0, NULL, NULL);
  if (len <= 0) {
    free(wfilename);
    return AVERROR(EINVAL);
  }
  char *local_filename = malloc(len);
  if (!local_filename) {
    free(wfilename);
    return AVERROR(ENOMEM);
  }
  WideCharToMultiByte(CP_UTF8, 0, wfilename, -1, local_filename, len, NULL, NULL);
  int ret = avformat_open_input(fmt_ctx, local_filename, NULL, NULL);
  free(local_filename);
  free(wfilename);
  return ret;
#else
  return avformat_open_input(fmt_ctx, filename, NULL, NULL);
#endif
}

int open_output_file_utf8(AVIOContext **pb, const char *filename) {
#ifdef _WIN32
  int wlen = MultiByteToWideChar(CP_UTF8, 0, filename, -1, NULL, 0);
  if (wlen <= 0) return AVERROR(EINVAL);
  wchar_t *wfilename = malloc(wlen * sizeof(wchar_t));
  if (!wfilename) return AVERROR(ENOMEM);
  MultiByteToWideChar(CP_UTF8, 0, filename, -1, wfilename, wlen);
  int len = WideCharToMultiByte(CP_UTF8, 0, wfilename, -1, NULL, 0, NULL, NULL);
  if (len <= 0) {
    free(wfilename);
    return AVERROR(EINVAL);
  }
  char *local_filename = malloc(len);
  if (!local_filename) {
    free(wfilename);
    return AVERROR(ENOMEM);
  }
  WideCharToMultiByte(CP_UTF8, 0, wfilename, -1, local_filename, len, NULL, NULL);
  int ret = avio_open(pb, local_filename, AVIO_FLAG_WRITE);
  free(local_filename);
  free(wfilename);
  return ret;
#else
  return avio_open(pb, filename, AVIO_FLAG_WRITE);
#endif
}