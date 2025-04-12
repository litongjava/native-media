#include "com_litongjava_media_NativeMedia.h"
#include <jni.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/timestamp.h>
#include <libavutil/error.h>

#ifdef _WIN32

#include <windows.h>
#include <stringapiset.h>

/**
 * 将 Java 的 jstring 转换为 UTF-8 编码的 C 字符串（支持中文路径）
 */
char *jstringToChar(JNIEnv *env, jstring jStr) {
  const jchar *raw = (*env)->GetStringChars(env, jStr, NULL);
  jsize len = (*env)->GetStringLength(env, jStr);
  int size_needed = WideCharToMultiByte(CP_UTF8, 0, (LPCWCH) raw, len, NULL, 0, NULL, NULL);
  char *strTo = (char *) malloc(size_needed + 1);
  WideCharToMultiByte(CP_UTF8, 0, (LPCWCH) raw, len, strTo, size_needed, NULL, NULL);
  strTo[size_needed] = '\0';
  (*env)->ReleaseStringChars(env, jStr, raw);
  return strTo;
}

#else
/**
 * 非 Windows 平台直接返回 UTF-8 编码的字符串
 */
char* jstringToChar(JNIEnv* env, jstring jStr) {
    const char* chars = (*env)->GetStringUTFChars(env, jStr, NULL);
    char* copy = strdup(chars);
    (*env)->ReleaseStringUTFChars(env, jStr, chars);
    return copy;
}
#endif

/*
 * 说明：
 * 1. 根据输出文件名创建输出格式上下文，并以第一个输入文件为模板建立输出流（仅复制视频和音频流参数）。
 * 2. 为每个输入文件计算其总体时长（按 AV_TIME_BASE 统一计算，取视频和音频最大值），并使用统一的 global_offset 作为所有流包的时间补偿。
 * 3. 每个包在转换时间戳时先使用 av_rescale_q 将其 pts/dts 从输入流的 time_base 转换到输出流 time_base，再加上统一偏移量。
 *
 * 这样处理后，各输入文件无论音视频各自时长是否一致，都按同一全局时间轴排列，解决了音频播放速度快于视频的问题。
 */
JNIEXPORT jboolean JNICALL Java_com_litongjava_media_NativeMedia_merge
  (JNIEnv *env, jclass clazz, jobjectArray jInputPaths, jstring jOutputPath) {
  int ret = 0;
  int nb_inputs = (*env)->GetArrayLength(env, jInputPaths);
  if (nb_inputs < 1) {
    return JNI_FALSE;
  }
  // 输出文件名（支持中文）
  char *output_filename = jstringToChar(env, jOutputPath);

  // 创建输出格式上下文
  AVFormatContext *ofmt_ctx = NULL;
  ret = avformat_alloc_output_context2(&ofmt_ctx, NULL, NULL, output_filename);
  if (ret < 0 || !ofmt_ctx) {
    free(output_filename);
    return JNI_FALSE;
  }
  AVOutputFormat *ofmt = ofmt_ctx->oformat;

  // 采用第一个输入文件作为模板构造输出流
  char *first_input = jstringToChar(env, (*env)->GetObjectArrayElement(env, jInputPaths, 0));
  AVFormatContext *ifmt_ctx1 = NULL;
  if ((ret = avformat_open_input(&ifmt_ctx1, first_input, NULL, NULL)) < 0) {
    free(first_input);
    free(output_filename);
    return JNI_FALSE;
  }
  if ((ret = avformat_find_stream_info(ifmt_ctx1, NULL)) < 0) {
    avformat_close_input(&ifmt_ctx1);
    free(first_input);
    free(output_filename);
    return JNI_FALSE;
  }
  free(first_input);

  // 建立输出流（仅复制视频、音频流）
  int video_out_index = -1, audio_out_index = -1;
  for (unsigned int i = 0; i < ifmt_ctx1->nb_streams; i++) {
    AVStream *in_stream = ifmt_ctx1->streams[i];
    if (in_stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO ||
        in_stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {

      AVStream *out_stream = avformat_new_stream(ofmt_ctx, NULL);
      if (!out_stream) {
        avformat_close_input(&ifmt_ctx1);
        free(output_filename);
        return JNI_FALSE;
      }
      ret = avcodec_parameters_copy(out_stream->codecpar, in_stream->codecpar);
      if (ret < 0) {
        avformat_close_input(&ifmt_ctx1);
        free(output_filename);
        return JNI_FALSE;
      }
      out_stream->codecpar->codec_tag = 0;
      if (in_stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
        video_out_index = out_stream->index;
      else if (in_stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
        audio_out_index = out_stream->index;
    }
  }
  avformat_close_input(&ifmt_ctx1);

  // 打开输出文件
  if (!(ofmt->flags & AVFMT_NOFILE)) {
    if ((ret = avio_open(&ofmt_ctx->pb, output_filename, AVIO_FLAG_WRITE)) < 0) {
      avformat_free_context(ofmt_ctx);
      free(output_filename);
      return JNI_FALSE;
    }
  }

  // 写入输出文件头
  ret = avformat_write_header(ofmt_ctx, NULL);
  if (ret < 0) {
    if (!(ofmt->flags & AVFMT_NOFILE))
      avio_close(ofmt_ctx->pb);
    avformat_free_context(ofmt_ctx);
    free(output_filename);
    return JNI_FALSE;
  }

  // 定义统一全局时间偏移量（单位：AV_TIME_BASE，AV_TIME_BASE_Q= {1,AV_TIME_BASE}）
  int64_t global_offset = 0;

  // 遍历每个输入文件
  for (int i = 0; i < nb_inputs; i++) {
    char *input_filename = jstringToChar(env, (*env)->GetObjectArrayElement(env, jInputPaths, i));
    AVFormatContext *ifmt_ctx = NULL;
    if ((ret = avformat_open_input(&ifmt_ctx, input_filename, NULL, NULL)) < 0) {
      free(input_filename);
      continue;  // 无法打开的文件跳过
    }
    if ((ret = avformat_find_stream_info(ifmt_ctx, NULL)) < 0) {
      avformat_close_input(&ifmt_ctx);
      free(input_filename);
      continue;
    }

    // 计算该输入文件中音视频的最大时长（统一转换到 AV_TIME_BASE 单位）
    int64_t file_duration = 0;
    for (unsigned int j = 0; j < ifmt_ctx->nb_streams; j++) {
      AVStream *in_stream = ifmt_ctx->streams[j];
      if (in_stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO ||
          in_stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
        if (in_stream->duration > 0) {
          int64_t dur = av_rescale_q(in_stream->duration, in_stream->time_base, AV_TIME_BASE_Q);
          if (dur > file_duration)
            file_duration = dur;
        }
      }
    }

    // 逐包读取处理
    AVPacket pkt;
    while (av_read_frame(ifmt_ctx, &pkt) >= 0) {
      AVStream *in_stream = ifmt_ctx->streams[pkt.stream_index];
      int out_index = -1;
      if (in_stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && video_out_index >= 0) {
        out_index = video_out_index;
      } else if (in_stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && audio_out_index >= 0) {
        out_index = audio_out_index;
      } else {
        av_packet_unref(&pkt);
        continue;
      }

      AVStream *out_stream = ofmt_ctx->streams[out_index];
      // 将统一偏移量转换到输出流的 time_base 单位
      int64_t offset = av_rescale_q(global_offset, AV_TIME_BASE_Q, out_stream->time_base);

      // 将 pts、dts 和 duration 从输入流 time_base 转换到输出流 time_base 后加上偏移量
      pkt.pts = av_rescale_q(pkt.pts, in_stream->time_base, out_stream->time_base) + offset;
      pkt.dts = av_rescale_q(pkt.dts, in_stream->time_base, out_stream->time_base) + offset;
      if (pkt.duration > 0)
        pkt.duration = av_rescale_q(pkt.duration, in_stream->time_base, out_stream->time_base);
      pkt.pos = -1;
      pkt.stream_index = out_index;

      ret = av_interleaved_write_frame(ofmt_ctx, &pkt);
      if (ret < 0) {
        av_packet_unref(&pkt);
        break;
      }
      av_packet_unref(&pkt);
    }

    avformat_close_input(&ifmt_ctx);
    free(input_filename);

    // 当前输入文件处理完后，将全局偏移量更新为之前的 global_offset + 本文件最大时长（确保所有流时间统一）
    global_offset += file_duration;
  }

  // 写入 trailer，并关闭上下文
  av_write_trailer(ofmt_ctx);
  if (!(ofmt->flags & AVFMT_NOFILE))
    avio_close(ofmt_ctx->pb);
  avformat_free_context(ofmt_ctx);
  free(output_filename);
  return JNI_TRUE;
}
