// pure_mp4_to_hls.c

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/opt.h>
#include "native_media.h"

#ifdef _WIN32

#include <windows.h>

#endif

// 辅助函数：打印 FFmpeg 错误信息
static void print_error(const char *msg, int errnum) {
  char errbuf[128] = {0};
  av_strerror(errnum, errbuf, sizeof(errbuf));
  fprintf(stderr, "%s: %s\n", msg, errbuf);
}

/**
 * 纯 C 接口：将 MP4 文件转换为 HLS 分段
 *
 * @param playlistUrl     播放列表文件路径（或 URL）
 * @param inputMp4Path    输入 MP4 文件路径
 * @param tsPattern       TS 分段文件命名模板（含完整路径），例如 "segment_%03d.ts"
 * @param segmentDuration TS 分段的时长（秒）
 * @param sceneIndex      分段起始编号
 *
 * @return 返回处理结果的描述字符串
 */
const char *split_video_to_hls(const char *playlistUrl, const char *inputMp4Path,
                               const char *tsPattern, int segmentDuration) {
  int ret = 0;
  AVFormatContext *ifmt_ctx = NULL;
  AVFormatContext *ofmt_ctx = NULL;
  AVDictionary *opts = NULL;
  int *stream_mapping = NULL;
  int stream_mapping_size = 0;
  AVPacket pkt;

  // 检查播放列表文件是否已经含有 "#EXT-X-ENDLIST"
  {
    FILE *checkFile = NULL;
#ifdef _WIN32
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, playlistUrl, -1, NULL, 0);
    wchar_t *wPlaylistUrl = (wchar_t *) malloc(size_needed * sizeof(wchar_t));
    if (wPlaylistUrl) {
      MultiByteToWideChar(CP_UTF8, 0, playlistUrl, -1, wPlaylistUrl, size_needed);
      checkFile = _wfopen(wPlaylistUrl, L"r");
      free(wPlaylistUrl);
    }
#else
    checkFile = fopen(playlistUrl, "r");
#endif
    if (checkFile) {
      fseek(checkFile, 0, SEEK_END);
      long size = ftell(checkFile);
      fseek(checkFile, 0, SEEK_SET);
      char *content = (char *) malloc(size + 1);
      if (content) {
        fread(content, 1, size, checkFile);
        content[size] = '\0';
        if (strstr(content, "#EXT-X-ENDLIST") != NULL) {
          fclose(checkFile);
          free(content);
          return "Playlist already contains #EXT-X-ENDLIST, cannot append";
        }
        free(content);
      }
      fclose(checkFile);
    }
  }

  // 打开输入 MP4 文件并获取流信息
  if ((ret = avformat_open_input(&ifmt_ctx, inputMp4Path, NULL, NULL)) < 0) {
    print_error("Unable to open input file", ret);
    goto end;
  }
  if ((ret = avformat_find_stream_info(ifmt_ctx, NULL)) < 0) {
    print_error("Unable to retrieve stream info", ret);
    goto end;
  }

  // 分配输出上下文，使用 "hls" Muxer，输出目标为 playlistUrl
  ret = avformat_alloc_output_context2(&ofmt_ctx, NULL, "hls", playlistUrl);
  if (ret < 0 || !ofmt_ctx) {
    print_error("Unable to allocate output context", ret);
    goto end;
  }

  // 设置 HLS 选项：分段时长、起始编号、分段文件命名模板、追加模式、不限制列表长度、事件类型
  {
    char seg_time_str[16] = {0};
    snprintf(seg_time_str, sizeof(seg_time_str), "%d", segmentDuration);
    av_dict_set(&opts, "hls_time", seg_time_str, 0);

    av_dict_set(&opts, "hls_segment_filename", tsPattern, 0);
    av_dict_set(&opts, "hls_flags", "append_list", 0);
    av_dict_set(&opts, "hls_list_size", "0", 0);
    av_dict_set(&opts, "hls_playlist_type", "event", 0);
  }

  // 为每个有效输入流（视频、音频、字幕）创建对应的输出流
  stream_mapping_size = ifmt_ctx->nb_streams;
  stream_mapping = av_malloc_array(stream_mapping_size, sizeof(int));
  if (!stream_mapping) {
    ret = AVERROR(ENOMEM);
    goto end;
  }
  for (int i = 0, j = 0; i < ifmt_ctx->nb_streams; i++) {
    AVStream *in_stream = ifmt_ctx->streams[i];
    AVCodecParameters *in_codecpar = in_stream->codecpar;
    if (in_codecpar->codec_type != AVMEDIA_TYPE_VIDEO &&
        in_codecpar->codec_type != AVMEDIA_TYPE_AUDIO &&
        in_codecpar->codec_type != AVMEDIA_TYPE_SUBTITLE) {
      stream_mapping[i] = -1;
      continue;
    }
    stream_mapping[i] = j++;
    AVStream *out_stream = avformat_new_stream(ofmt_ctx, NULL);
    if (!out_stream) {
      ret = AVERROR_UNKNOWN;
      goto end;
    }
    ret = avcodec_parameters_copy(out_stream->codecpar, in_codecpar);
    if (ret < 0) {
      print_error("Failed to copy codec parameters", ret);
      goto end;
    }
    out_stream->codecpar->codec_tag = 0;
    out_stream->time_base = in_stream->time_base;
  }

  // 打开输出文件（如果 Muxer 要求打开文件）
  if (!(ofmt_ctx->oformat->flags & AVFMT_NOFILE)) {
#ifdef _WIN32
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, playlistUrl, -1, NULL, 0);
    wchar_t *wPlaylistUrl = (wchar_t *) malloc(size_needed * sizeof(wchar_t));
    if (wPlaylistUrl) {
      MultiByteToWideChar(CP_UTF8, 0, playlistUrl, -1, wPlaylistUrl, size_needed);
      ret = avio_open(&ofmt_ctx->pb, wPlaylistUrl, AVIO_FLAG_WRITE);
      free(wPlaylistUrl);
    }
#else
    ret = avio_open(&ofmt_ctx->pb, playlistUrl, AVIO_FLAG_WRITE);
#endif
    if (ret < 0) {
      print_error("Unable to open output file", ret);
      goto end;
    }
  }

  // 写入 Muxer 文件头，启动生成 TS 分段及播放列表更新
  ret = avformat_write_header(ofmt_ctx, &opts);
  if (ret < 0) {
    print_error("Failed to write header", ret);
    goto end;
  }

  // 读取输入数据包，将时间戳转换后写入输出
  while (av_read_frame(ifmt_ctx, &pkt) >= 0) {
    if (pkt.stream_index >= ifmt_ctx->nb_streams ||
        stream_mapping[pkt.stream_index] < 0) {
      av_packet_unref(&pkt);
      continue;
    }
    AVStream *in_stream = ifmt_ctx->streams[pkt.stream_index];
    AVStream *out_stream = ofmt_ctx->streams[stream_mapping[pkt.stream_index]];

    pkt.pts = av_rescale_q_rnd(pkt.pts, in_stream->time_base, out_stream->time_base,
                               AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
    pkt.dts = av_rescale_q_rnd(pkt.dts, in_stream->time_base, out_stream->time_base,
                               AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
    pkt.duration = av_rescale_q(pkt.duration, in_stream->time_base, out_stream->time_base);
    pkt.pos = -1;
    pkt.stream_index = stream_mapping[pkt.stream_index];

    ret = av_interleaved_write_frame(ofmt_ctx, &pkt);
    if (ret < 0) {
      print_error("Failed to write packet", ret);
      break;
    }
    av_packet_unref(&pkt);
  }

  // 结束时调用 trailer（本例中依然调用 trailer 以更新播放列表，如果需要避免写入 #EXT-X-ENDLIST 可注释）
  av_write_trailer(ofmt_ctx);

  end:
  if (stream_mapping)
    av_freep(&stream_mapping);
  if (ifmt_ctx)
    avformat_close_input(&ifmt_ctx);
  if (ofmt_ctx) {
    if (!(ofmt_ctx->oformat->flags & AVFMT_NOFILE))
      avio_closep(&ofmt_ctx->pb);
    avformat_free_context(ofmt_ctx);
  }
  av_dict_free(&opts);

  if (ret < 0) {
    return "HLS segmentation failed";
  } else {
    static char resultMsg[256];
    snprintf(resultMsg, sizeof(resultMsg),
             "HLS segmentation successful: generated new segments and appended to playlist %s", playlistUrl);
    return resultMsg;
  }
}
