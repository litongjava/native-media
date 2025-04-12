#include "com_litongjava_media_NativeMedia.h"
#include <jni.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#ifdef _WIN32

#include <windows.h>

#endif

// FFmpeg Headers
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/opt.h>
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/mathematics.h>
#include <libavutil/mem.h>
#include <libavutil/samplefmt.h>
#include <libavutil/timestamp.h>

inline int av_channel_layout_copy(AVChannelLayout *dst, const AVChannelLayout *src) {
  if (!dst || !src) return AVERROR(EINVAL);
  memcpy(dst, src, sizeof(*dst));
  dst->opaque = NULL; // 简化处理，不复制opaque字段
  return 0;
}

typedef struct HlsSession {
  AVFormatContext *ofmt_ctx;    // HLS muxer 输出上下文
  int segDuration;              // 分段时长（秒）
  int nextSegmentNumber;        // 当前下一个分段编号
  int64_t global_offset;        // 全局时间戳偏移量
  char *ts_pattern;             // TS 分段文件命名模板（深拷贝保存）
  int header_written;           // 标识是否已写 header
  AVDictionary *opts;           // 保存 HLS 配置选项
} HlsSession;

JNIEXPORT jlong JNICALL Java_com_litongjava_media_NativeMedia_initPersistentHls
  (JNIEnv *env, jclass clazz, jstring playlistUrlJ, jstring tsPatternJ, jint startNumber, jint segDuration) {

  const char *playlistUrl = (*env)->GetStringUTFChars(env, playlistUrlJ, NULL);
  const char *tsPattern = (*env)->GetStringUTFChars(env, tsPatternJ, NULL);

  HlsSession *session = (HlsSession *) malloc(sizeof(HlsSession));
  if (!session) {
    (*env)->ReleaseStringUTFChars(env, playlistUrlJ, playlistUrl);
    (*env)->ReleaseStringUTFChars(env, tsPatternJ, tsPattern);
    return 0;
  }
  memset(session, 0, sizeof(HlsSession));
  session->segDuration = segDuration;
  session->nextSegmentNumber = startNumber;
  session->global_offset = 0;
  session->header_written = 0; // header 尚未写入
  session->ts_pattern = strdup(tsPattern);
  if (!session->ts_pattern) {
    free(session);
    (*env)->ReleaseStringUTFChars(env, playlistUrlJ, playlistUrl);
    (*env)->ReleaseStringUTFChars(env, tsPatternJ, tsPattern);
    return 0;
  }

  // 创建输出上下文，指定 muxer 为 "hls"，目标为 playlistUrl
  AVFormatContext *ofmt_ctx = NULL;
  int ret = avformat_alloc_output_context2(&ofmt_ctx, NULL, "hls", playlistUrl);
  if (ret < 0 || !ofmt_ctx) {
    free(session->ts_pattern);
    free(session);
    (*env)->ReleaseStringUTFChars(env, playlistUrlJ, playlistUrl);
    (*env)->ReleaseStringUTFChars(env, tsPatternJ, tsPattern);
    return 0;
  }

  // 构造 HLS 选项字典，并保存到 session->opts
  AVDictionary *opts = NULL;
  char seg_time_str[16] = {0};
  snprintf(seg_time_str, sizeof(seg_time_str), "%d", segDuration);
  av_dict_set(&opts, "hls_time", seg_time_str, 0);
  // 使用用户指定的 tsPattern（如 "segment_video_%03d.ts"），保证生成的分段文件名符合预期
  av_dict_set(&opts, "hls_segment_filename", tsPattern, 0);
  av_dict_set(&opts, "hls_flags", "append_list", 0);
  // 设置 hls_list_size 为 0 表示不限制条数
  av_dict_set(&opts, "hls_list_size", "0", 0);
  av_dict_set(&opts, "hls_playlist_type", "event", 0);
  char start_num_str[16] = {0};
  snprintf(start_num_str, sizeof(start_num_str), "%d", startNumber);
  av_dict_set(&opts, "start_number", start_num_str, 0);
  session->opts = opts;

  // 如果输出格式要求打开文件，则调用 avio_open
  if (!(ofmt_ctx->oformat->flags & AVFMT_NOFILE)) {
    ret = avio_open(&ofmt_ctx->pb, playlistUrl, AVIO_FLAG_WRITE);
    if (ret < 0) {
      av_dict_free(&opts);
      avformat_free_context(ofmt_ctx);
      free(session->ts_pattern);
      free(session);
      (*env)->ReleaseStringUTFChars(env, playlistUrlJ, playlistUrl);
      (*env)->ReleaseStringUTFChars(env, tsPatternJ, tsPattern);
      return 0;
    }
  }

  session->ofmt_ctx = ofmt_ctx;

  (*env)->ReleaseStringUTFChars(env, playlistUrlJ, playlistUrl);
  (*env)->ReleaseStringUTFChars(env, tsPatternJ, tsPattern);

  return (jlong) session;
}

/*
 * Class:     com_litongjava_media_NativeMedia
 * Method:    finishPersistentHls
 * Signature: (JLjava/lang/String;)Ljava/lang/String;
 *
 * 实现说明：
 * 1. 根据传入的会话指针，取出 HLS 会话结构体；
 * 2. 调用 av_write_trailer 写入 trailer（如生成 EXT‑X‑ENDLIST 标签），关闭输出流；
 * 3. 释放输出上下文以及会话中分配的资源（例如 TS 模板字符串和会话结构体）；
 * 4. 返回结束操作的状态信息。
 */
JNIEXPORT jstring JNICALL Java_com_litongjava_media_NativeMedia_finishPersistentHls
  (JNIEnv *env, jclass clazz, jlong sessionPtr, jstring playlistUrlJ) {

  // 从 sessionPtr 恢复会话结构体
  HlsSession *session = (HlsSession *) sessionPtr;
  if (!session) {
    return (*env)->NewStringUTF(env, "Invalid session pointer");
  }

  // 从 Java 字符串中获取播放列表路径（用于返回消息）
  const char *playlistUrl = (*env)->GetStringUTFChars(env, playlistUrlJ, NULL);

  // 写入 trailer，结束会话
  int ret = av_write_trailer(session->ofmt_ctx);
  if (ret < 0) {
    (*env)->ReleaseStringUTFChars(env, playlistUrlJ, playlistUrl);
    // 尽管写 trailer 失败，此处也应关闭资源以避免泄漏
    if (!(session->ofmt_ctx->oformat->flags & AVFMT_NOFILE))
      avio_closep(&session->ofmt_ctx->pb);
    avformat_free_context(session->ofmt_ctx);
    free(session->ts_pattern);
    free(session);
    return (*env)->NewStringUTF(env, "Failed to write trailer");
  }

  // 关闭输出文件（如有必要），释放输出上下文
  if (!(session->ofmt_ctx->oformat->flags & AVFMT_NOFILE))
    avio_closep(&session->ofmt_ctx->pb);
  avformat_free_context(session->ofmt_ctx);

  // 释放会话中分配的资源
  free(session->ts_pattern);
  free(session);

  // 构造返回消息
  char resultMsg[256] = {0};
  snprintf(resultMsg, sizeof(resultMsg),
           "Persistent HLS session finished successfully: playlist %s", playlistUrl);

  (*env)->ReleaseStringUTFChars(env, playlistUrlJ, playlistUrl);
  return (*env)->NewStringUTF(env, resultMsg);
}

JNIEXPORT jstring JNICALL Java_com_litongjava_media_NativeMedia_appendVideoSegmentToHls
  (JNIEnv *env, jclass clazz, jlong sessionPtr, jstring inputFilePathJ) {
  HlsSession *session = (HlsSession *) sessionPtr;
  if (!session || !session->ofmt_ctx) {
    return (*env)->NewStringUTF(env, "Invalid HLS session pointer");
  }

  const char *inputFilePath = (*env)->GetStringUTFChars(env, inputFilePathJ, NULL);
  if (!inputFilePath) {
    return (*env)->NewStringUTF(env, "Failed to get input file path");
  }

  AVFormatContext *ifmt_ctx = NULL;
  int ret = avformat_open_input(&ifmt_ctx, inputFilePath, NULL, NULL);
  if (ret < 0) {
    char errbuf[128] = {0};
    av_strerror(ret, errbuf, sizeof(errbuf));
    (*env)->ReleaseStringUTFChars(env, inputFilePathJ, inputFilePath);
    char msg[256] = {0};
    snprintf(msg, sizeof(msg), "Failed to open input file: %s", errbuf);
    return (*env)->NewStringUTF(env, msg);
  }

  ret = avformat_find_stream_info(ifmt_ctx, NULL);
  if (ret < 0) {
    avformat_close_input(&ifmt_ctx);
    (*env)->ReleaseStringUTFChars(env, inputFilePathJ, inputFilePath);
    return (*env)->NewStringUTF(env, "Failed to retrieve stream info from input file");
  }

  // 如果 persistent HLS 会话中还没有输出流，则首次追加：
  if (session->ofmt_ctx->nb_streams == 0) {
    for (unsigned int i = 0; i < ifmt_ctx->nb_streams; i++) {
      AVStream *in_stream = ifmt_ctx->streams[i];
      if (in_stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO ||
          in_stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
        AVStream *out_stream = avformat_new_stream(session->ofmt_ctx, NULL);
        if (!out_stream) continue;
        ret = avcodec_parameters_copy(out_stream->codecpar, in_stream->codecpar);
        if (ret < 0) continue;
        out_stream->codecpar->codec_tag = 0;
        out_stream->time_base = in_stream->time_base;
      }
    }
    // 使用保存的 opts 调用 avformat_write_header
    ret = avformat_write_header(session->ofmt_ctx, &session->opts);
    if (ret < 0) {
      avformat_close_input(&ifmt_ctx);
      (*env)->ReleaseStringUTFChars(env, inputFilePathJ, inputFilePath);
      return (*env)->NewStringUTF(env, "Failed to write header on first segment append");
    }
    session->header_written = 1;
    // 释放 opts 后不再需要
    av_dict_free(&session->opts);
  }

  // 计算输入文件音视频流的最大时长（单位转换为 AV_TIME_BASE）
  int64_t file_duration = 0;
  for (unsigned int i = 0; i < ifmt_ctx->nb_streams; i++) {
    AVStream *in_stream = ifmt_ctx->streams[i];
    if (in_stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO ||
        in_stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
      if (in_stream->duration > 0) {
        int64_t dur = av_rescale_q(in_stream->duration, in_stream->time_base, AV_TIME_BASE_Q);
        if (dur > file_duration)
          file_duration = dur;
      }
    }
  }

  AVPacket pkt;
  while (av_read_frame(ifmt_ctx, &pkt) >= 0) {
    AVStream *in_stream = ifmt_ctx->streams[pkt.stream_index];
    if (in_stream->codecpar->codec_type != AVMEDIA_TYPE_VIDEO &&
        in_stream->codecpar->codec_type != AVMEDIA_TYPE_AUDIO) {
      av_packet_unref(&pkt);
      continue;
    }

    // 按流类型匹配：假设只有一个视频流和一个音频流
    int out_index = -1;
    for (unsigned int j = 0; j < session->ofmt_ctx->nb_streams; j++) {
      AVStream *out_stream = session->ofmt_ctx->streams[j];
      if (out_stream->codecpar->codec_type == in_stream->codecpar->codec_type) {
        out_index = j;
        break;
      }
    }
    if (out_index < 0) {
      av_packet_unref(&pkt);
      continue;
    }
    AVStream *out_stream = session->ofmt_ctx->streams[out_index];

    // 将 pkt 时间戳转换后加上全局偏移
    int64_t offset = av_rescale_q(session->global_offset, AV_TIME_BASE_Q, out_stream->time_base);
    pkt.pts = av_rescale_q(pkt.pts, in_stream->time_base, out_stream->time_base) + offset;
    pkt.dts = av_rescale_q(pkt.dts, in_stream->time_base, out_stream->time_base) + offset;
    if (pkt.duration > 0)
      pkt.duration = av_rescale_q(pkt.duration, in_stream->time_base, out_stream->time_base);
    pkt.pos = -1;
    pkt.stream_index = out_index;

    ret = av_interleaved_write_frame(session->ofmt_ctx, &pkt);
    if (ret < 0) {
      av_packet_unref(&pkt);
      break;
    }
    av_packet_unref(&pkt);
  }

  // 累计更新全局时间偏移
  session->global_offset += file_duration;

  avformat_close_input(&ifmt_ctx);
  (*env)->ReleaseStringUTFChars(env, inputFilePathJ, inputFilePath);

  char resultMsg[256] = {0};
  snprintf(resultMsg, sizeof(resultMsg),
           "Appended video segment successfully, updated global offset to %lld", session->global_offset);
  return (*env)->NewStringUTF(env, resultMsg);
}
