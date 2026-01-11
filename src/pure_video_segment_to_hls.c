#include "native_media.h"
#include <libavutil/avutil.h>
#include <libavutil/timestamp.h>

// 初始化 HLS 会话
HlsSession *init_hls_session(const char *playlistUrl, const char *tsPattern, int startNumber, int segDuration) {
  HlsSession *session = (HlsSession *) malloc(sizeof(HlsSession));
  if (!session) return NULL;
  memset(session, 0, sizeof(HlsSession));

  session->segDuration = segDuration;
  session->ts_pattern = strdup(tsPattern);
  session->created_time = time(NULL);
  session->global_offset = 0; // 初始偏移为0

  // 创建输出上下文
  int ret = avformat_alloc_output_context2(&session->ofmt_ctx, NULL, "hls", playlistUrl);
  if (ret < 0 || !session->ofmt_ctx) {
    free(session->ts_pattern);
    free(session);
    return NULL;
  }

  // 设置 HLS 选项
  char seg_time_str[16] = {0};
  snprintf(seg_time_str, sizeof(seg_time_str), "%d", segDuration);
  av_dict_set(&session->opts, "hls_time", seg_time_str, 0);
  av_dict_set(&session->opts, "hls_segment_filename", tsPattern, 0);
  av_dict_set(&session->opts, "hls_flags", "append_list", 0); // 关键：追加模式
  av_dict_set(&session->opts, "hls_list_size", "0", 0);       // 保留所有切片
  av_dict_set(&session->opts, "hls_playlist_type", "event", 0);

  char start_num_str[16] = {0};
  snprintf(start_num_str, sizeof(start_num_str), "%d", startNumber);
  av_dict_set(&session->opts, "start_number", start_num_str, 0);

  // 打开输出文件 (playlist)
  if (!(session->ofmt_ctx->oformat->flags & AVFMT_NOFILE)) {
    ret = avio_open(&session->ofmt_ctx->pb, playlistUrl, AVIO_FLAG_WRITE);
    if (ret < 0) {
      av_dict_free(&session->opts);
      avformat_free_context(session->ofmt_ctx);
      free(session->ts_pattern);
      free(session);
      return NULL;
    }
  }

  return session;
}

#include <libavutil/opt.h>

/**
 * 追加一个视频文件到现有的 HLS 会话中
 * 1. 使用 av_interleaved_write_frame(ctx, NULL) 强制闭合当前文件的 TS 切片。
 * 2. 动态设置 discont_start 标志，确保 M3U8 插入 #EXT-X-DISCONTINUITY。
 * 3. 严格同步处理 PTS 和 DTS，解决由于 B 帧导致的顺序混乱问题。
 */
char *append_video_segment(HlsSession *session, const char *inputFilePath) {
  if (!session || !session->ofmt_ctx) return strdup("Invalid HLS session");

  AVFormatContext *ifmt_ctx = NULL;
  int ret = avformat_open_input(&ifmt_ctx, inputFilePath, NULL, NULL);
  if (ret < 0) {
    char errbuf[128];
    av_strerror(ret, errbuf, sizeof(errbuf));
    return strdup(errbuf);
  }

  if (avformat_find_stream_info(ifmt_ctx, NULL) < 0) {
    avformat_close_input(&ifmt_ctx);
    return strdup("Failed to find stream info");
  }

  // --- 1. 自动初始化输出流 (第一次追加时) ---
  if (!session->header_written) {
    for (unsigned int i = 0; i < ifmt_ctx->nb_streams; i++) {
      AVStream *in_stream = ifmt_ctx->streams[i];
      if (in_stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO ||
          in_stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
        AVStream *out_stream = avformat_new_stream(session->ofmt_ctx, NULL);
        if (!out_stream) continue;
        avcodec_parameters_copy(out_stream->codecpar, in_stream->codecpar);
        out_stream->codecpar->codec_tag = 0;
      }
    }
    // 使用 init 时保存的配置写入头部
    ret = avformat_write_header(session->ofmt_ctx, &session->opts);
    if (ret < 0) {
      avformat_close_input(&ifmt_ctx);
      return strdup("Failed to write HLS header");
    }
    session->header_written = 1;
  }

  // --- 2. 注入 Discontinuity 标记 ---
  av_opt_set(session->ofmt_ctx->priv_data, "hls_flags", "discont_start", AV_OPT_SEARCH_CHILDREN);

  int64_t max_pts_in_us = session->global_offset;
  AVPacket pkt;

  // --- 3. 数据重封装循环 ---
  while (av_read_frame(ifmt_ctx, &pkt) >= 0) {
    AVStream *in_stream = ifmt_ctx->streams[pkt.stream_index];

    // 匹配输出流
    int out_index = -1;
    for (unsigned int j = 0; j < session->ofmt_ctx->nb_streams; j++) {
      if (session->ofmt_ctx->streams[j]->codecpar->codec_type == in_stream->codecpar->codec_type) {
        out_index = j;
        break;
      }
    }

    if (out_index >= 0) {
      AVStream *out_stream = session->ofmt_ctx->streams[out_index];

      // 计算该流在当前 Timebase 下的起始偏移量
      // $$offset_{pts} = \text{rescale}(global\_offset, AV\_TIME\_BASE, out\_stream->time\_base)$$
      int64_t offset_pts = av_rescale_q(session->global_offset, AV_TIME_BASE_Q, out_stream->time_base);

      // 转换 PTS/DTS (必须同时转换以支持 B 帧)
      if (pkt.pts != AV_NOPTS_VALUE) {
        pkt.pts = av_rescale_q(pkt.pts, in_stream->time_base, out_stream->time_base) + offset_pts;
      }
      if (pkt.dts != AV_NOPTS_VALUE) {
        pkt.dts = av_rescale_q(pkt.dts, in_stream->time_base, out_stream->time_base) + offset_pts;
      }
      if (pkt.duration > 0) {
        pkt.duration = av_rescale_q(pkt.duration, in_stream->time_base, out_stream->time_base);
      }

      pkt.stream_index = out_index;
      pkt.pos = -1;

      // 更新本次追加后的流结尾位置
      int64_t packet_end_pts = (pkt.pts != AV_NOPTS_VALUE ? pkt.pts : pkt.dts) + pkt.duration;
      int64_t packet_end_us = av_rescale_q(packet_end_pts, out_stream->time_base, AV_TIME_BASE_Q);
      if (packet_end_us > max_pts_in_us) {
        max_pts_in_us = packet_end_us;
      }

      av_interleaved_write_frame(session->ofmt_ctx, &pkt);
    }
    av_packet_unref(&pkt);
  }

  // --- 4. 强制 Flush
  av_interleaved_write_frame(session->ofmt_ctx, NULL);
  if (session->ofmt_ctx->pb) {
    avio_flush(session->ofmt_ctx->pb);
  }

  // 更新全局偏移量，增加 1 微秒的冗余以防时间戳重叠
  session->global_offset = max_pts_in_us + 1;

  avformat_close_input(&ifmt_ctx);
  return NULL; // 返回 NULL 表示成功
}

// 结束会话
char *finish_hls_session(HlsSession *session, const char *playlistUrl) {
  if (!session || session->closed) return strdup("Session invalid or closed");

  av_write_trailer(session->ofmt_ctx);

  if (!(session->ofmt_ctx->oformat->flags & AVFMT_NOFILE)) {
    avio_closep(&session->ofmt_ctx->pb);
  }
  avformat_free_context(session->ofmt_ctx);

  session->closed = 1;
  free_hls_session_struct(session);

  char msg[256];
  snprintf(msg, sizeof(msg), "Finished HLS: %s", playlistUrl);
  return strdup(msg);
}

void free_hls_session_struct(HlsSession *session) {
  if (session) {
    if (session->ts_pattern) free(session->ts_pattern);
    free(session);
  }
}