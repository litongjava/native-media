#include "com_litongjava_media_NativeMedia.h"
#include "native_media.h"
#include <jni.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <libavformat/avformat.h>
#include <libavutil/timestamp.h>

JNIEXPORT jdouble JNICALL Java_com_litongjava_media_NativeMedia_getVideoLength
  (JNIEnv *env, jclass clazz, jstring jInputPath) {
  // 将 Java 的 jstring 转换为 C 字符串，支持中文文件名
  char *input_filename = jstringToChar(env, jInputPath);
  if (!input_filename) {
    return -1;  // 转换失败
  }

  AVFormatContext *ifmt_ctx = NULL;
  int ret = 0;

  // 打开输入文件，支持中文路径
  ret = avformat_open_input(&ifmt_ctx, input_filename, NULL, NULL);
  if (ret < 0 || !ifmt_ctx) {
    free(input_filename);
    return -1;
  }

  // 获取媒体文件的流信息
  ret = avformat_find_stream_info(ifmt_ctx, NULL);
  if (ret < 0) {
    avformat_close_input(&ifmt_ctx);
    free(input_filename);
    return -1;
  }

  jdouble duration_in_seconds = 0.0;

  // 如果 AVFormatContext->duration 有效（单位为 AV_TIME_BASE 即微秒）
  if (ifmt_ctx->duration != AV_NOPTS_VALUE) {
    duration_in_seconds = (jdouble) ifmt_ctx->duration / AV_TIME_BASE;
  } else {
    // 否则遍历每个流，选择最大时长作为视频时长
    int64_t max_duration = 0;
    for (unsigned int i = 0; i < ifmt_ctx->nb_streams; i++) {
      AVStream *stream = ifmt_ctx->streams[i];
      if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO ||
          stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
        if (stream->duration > 0) {
          // 将该流的 duration 转换到统一时间基 AV_TIME_BASE 单位
          int64_t stream_duration = av_rescale_q(stream->duration, stream->time_base, (AVRational) {1, AV_TIME_BASE});
          if (stream_duration > max_duration) {
            max_duration = stream_duration;
          }
        }
      }
    }
    duration_in_seconds = (jdouble) max_duration / AV_TIME_BASE;
  }

  // 释放资源
  avformat_close_input(&ifmt_ctx);
  free(input_filename);

  return duration_in_seconds;
}
