#include "com_litongjava_media_NativeMedia.h"
#include <jni.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

// 包含 FFmpeg 头文件
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>

JNIEXPORT jstring JNICALL Java_com_litongjava_media_NativeMedia_addWatermarkToVideo
  (JNIEnv *env, jclass clazz, jstring inputVideoPathJ, jstring outputVideoPathJ, jstring watermarkTextJ,
   jstring fontFileJ) {
  // 从 Java 获取文件路径、水印文本和字体文件路径
  const char *inputPath = (*env)->GetStringUTFChars(env, inputVideoPathJ, NULL);
  const char *outputPath = (*env)->GetStringUTFChars(env, outputVideoPathJ, NULL);
  const char *watermarkText = (*env)->GetStringUTFChars(env, watermarkTextJ, NULL);

  const char *fontFile = NULL;
// 检查 fontFileJ 是否为 null 或者空字符串
  if (fontFileJ == NULL || (*env)->GetStringUTFLength(env, fontFileJ) == 0) {
#ifdef _WIN32
    // Windows 系统，示例路径
    fontFile = "C\\:/Windows/Fonts/simhei.ttf";
#elif defined(__APPLE__)

    fontFile = "/Library/Fonts/Arial Unicode.ttf";
#elif defined(__linux__)
    fontFile = "/usr/share/fonts/google-noto-cjk/NotoSansCJK-Regular.ttc";
#else

    fontFile = "/usr/share/fonts/default.ttf";
#endif
  } else {
    fontFile = (*env)->GetStringUTFChars(env, fontFileJ, NULL);
  }

  AVFormatContext *ifmt_ctx = NULL, *ofmt_ctx = NULL;
  AVCodecContext *dec_ctx = NULL, *enc_ctx = NULL;
  AVStream *in_video_stream = NULL, *out_video_stream = NULL;
  int video_stream_index = -1;
  int ret = 0;

  // 以下变量用于 FFmpeg 滤镜
  AVFilterGraph *filter_graph = NULL;
  AVFilterContext *buffersrc_ctx = NULL, *buffersink_ctx = NULL;
  const AVFilter *buffersrc = NULL, *buffersink = NULL;
  AVFilterInOut *outputs = NULL, *inputs = NULL;
  char filter_descr[512] = {0};

  // 打开输入文件
  if ((ret = avformat_open_input(&ifmt_ctx, inputPath, NULL, NULL)) < 0) {
    goto end_fail;
  }
  if ((ret = avformat_find_stream_info(ifmt_ctx, NULL)) < 0) {
    goto end_fail;
  }

  // 寻找视频流
  for (int i = 0; i < ifmt_ctx->nb_streams; i++) {
    if (ifmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
      video_stream_index = i;
      in_video_stream = ifmt_ctx->streams[i];
      break;
    }
  }
  if (video_stream_index < 0) {
    ret = -1;
    goto end_fail;
  }

  // 打开视频解码器
  AVCodec *dec = avcodec_find_decoder(in_video_stream->codecpar->codec_id);
  if (!dec) {
    ret = -1;
    goto end_fail;
  }
  dec_ctx = avcodec_alloc_context3(dec);
  if (!dec_ctx) {
    ret = -1;
    goto end_fail;
  }
  ret = avcodec_parameters_to_context(dec_ctx, in_video_stream->codecpar);
  if (ret < 0) {
    goto end_fail;
  }
  if ((ret = avcodec_open2(dec_ctx, dec, NULL)) < 0) {
    goto end_fail;
  }

  // 创建输出文件上下文
  avformat_alloc_output_context2(&ofmt_ctx, NULL, NULL, outputPath);
  if (!ofmt_ctx) {
    ret = -1;
    goto end_fail;
  }

  // 为输出文件创建一个新视频流
  out_video_stream = avformat_new_stream(ofmt_ctx, NULL);
  if (!out_video_stream) {
    ret = -1;
    goto end_fail;
  }

  // 使用 H.264 编码器进行编码
  AVCodec *encoder = avcodec_find_encoder(AV_CODEC_ID_H264);
  if (!encoder) {
    ret = -1;
    goto end_fail;
  }
  enc_ctx = avcodec_alloc_context3(encoder);
  if (!enc_ctx) {
    ret = -1;
    goto end_fail;
  }
  // 设置编码参数，根据输入视频设置输出参数（这里保持分辨率一致）
  enc_ctx->height = dec_ctx->height;
  enc_ctx->width = dec_ctx->width;
  enc_ctx->sample_aspect_ratio = dec_ctx->sample_aspect_ratio;
  // 选择 encoder 支持的像素格式（首选 encoder 的列表）
  enc_ctx->pix_fmt = encoder->pix_fmts ? encoder->pix_fmts[0] : dec_ctx->pix_fmt;
  // 时间基设置可以采用解码器的帧率倒数，或者直接使用输入流的时间基
  if (dec_ctx->framerate.num && dec_ctx->framerate.den)
    enc_ctx->time_base = av_inv_q(dec_ctx->framerate);
  else
    enc_ctx->time_base = in_video_stream->time_base;
  if (ofmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
    enc_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

  // 打开编码器
  AVDictionary *enc_opts = NULL;
  av_dict_set(&enc_opts, "preset", "veryfast", 0);
  if ((ret = avcodec_open2(enc_ctx, encoder, &enc_opts)) < 0) {
    av_dict_free(&enc_opts);
    goto end_fail;
  }
  av_dict_free(&enc_opts);

  // 将编码器参数复制到输出流
  ret = avcodec_parameters_from_context(out_video_stream->codecpar, enc_ctx);
  if (ret < 0) {
    goto end_fail;
  }
  out_video_stream->time_base = enc_ctx->time_base;

  // 打开输出文件（如果需要）
  if (!(ofmt_ctx->oformat->flags & AVFMT_NOFILE)) {
    ret = avio_open(&ofmt_ctx->pb, outputPath, AVIO_FLAG_WRITE);
    if (ret < 0) {
      goto end_fail;
    }
  }

  // -----------------------------
  //  1. 创建滤镜图（Filter Graph）
  // -----------------------------
  filter_graph = avfilter_graph_alloc();
  if (!filter_graph) {
    ret = -1;
    goto end_fail;
  }

  // 获取 buffer 源和 sink 滤镜
  buffersrc = avfilter_get_by_name("buffer");
  buffersink = avfilter_get_by_name("buffersink");
  if (!buffersrc || !buffersink) {
    ret = -1;
    goto end_fail;
  }

  // 构造 buffer 源的参数字符串
  char args[512] = {0};
  snprintf(args, sizeof(args),
           "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
           dec_ctx->width, dec_ctx->height, dec_ctx->pix_fmt,
           in_video_stream->time_base.num, in_video_stream->time_base.den,
           dec_ctx->sample_aspect_ratio.num, dec_ctx->sample_aspect_ratio.den);
  ret = avfilter_graph_create_filter(&buffersrc_ctx, buffersrc, "in", args, NULL, filter_graph);
  if (ret < 0) {
    goto end_fail;
  }

  ret = avfilter_graph_create_filter(&buffersink_ctx, buffersink, "out", NULL, NULL, filter_graph);
  if (ret < 0) {
    goto end_fail;
  }

  // 设置 buffersink 的输出像素格式，仅接受编码器需要的格式
  enum AVPixelFormat pix_fmts[] = {enc_ctx->pix_fmt, AV_PIX_FMT_NONE};
  ret = av_opt_set_int_list(buffersink_ctx, "pix_fmts", pix_fmts, AV_PIX_FMT_NONE, AV_OPT_SEARCH_CHILDREN);
  if (ret < 0) {
    goto end_fail;
  }

  // 构造 drawtext 滤镜描述字符串
  // 注意：text 参数需要使用 UTF-8 编码（支持中文），fontfile 必须指定支持中文的字体文件
  snprintf(filter_descr, sizeof(filter_descr),
           "drawtext=fontfile='%s':text='%s':x=w-tw-10:y=h-th-10:fontsize=24:fontcolor=white",
           fontFile, watermarkText);

  // 初始化滤镜的输入输出端点
  outputs = avfilter_inout_alloc();
  inputs = avfilter_inout_alloc();
  if (!outputs || !inputs) {
    ret = -1;
    goto end_fail;
  }

  outputs->name = av_strdup("in");
  outputs->filter_ctx = buffersrc_ctx;
  outputs->pad_idx = 0;
  outputs->next = NULL;

  inputs->name = av_strdup("out");
  inputs->filter_ctx = buffersink_ctx;
  inputs->pad_idx = 0;
  inputs->next = NULL;

  // 解析并构造滤镜链（在 buffersrc 与 buffersink 之间插入 drawtext 过滤器）
  ret = avfilter_graph_parse_ptr(filter_graph, filter_descr, &inputs, &outputs, NULL);
  if (ret < 0) {
    goto end_fail;
  }
  ret = avfilter_graph_config(filter_graph, NULL);
  if (ret < 0) {
    goto end_fail;
  }

  avfilter_inout_free(&inputs);
  avfilter_inout_free(&outputs);

  // -----------------------------
  // 写入输出文件的文件头
  ret = avformat_write_header(ofmt_ctx, NULL);
  if (ret < 0) {
    goto end_fail;
  }

  // 申请解码与滤镜使用的 AVFrame
  AVFrame *frame = av_frame_alloc();
  AVFrame *filt_frame = av_frame_alloc();
  if (!frame || !filt_frame) {
    ret = AVERROR(ENOMEM);
    goto end_fail;
  }

  AVPacket packet;
  av_init_packet(&packet);
  packet.data = NULL;
  packet.size = 0;

  // -----------------------------
  // 逐帧处理：解码 -> 送入滤镜 -> 从滤镜中取出 -> 编码 -> 写入文件
  while (av_read_frame(ifmt_ctx, &packet) >= 0) {
    if (packet.stream_index == video_stream_index) {
      ret = avcodec_send_packet(dec_ctx, &packet);
      if (ret < 0) {
        break;
      }
      while ((ret = avcodec_receive_frame(dec_ctx, frame)) >= 0) {
        // 将解码后的帧送入滤镜图
        if ((ret = av_buffersrc_add_frame(buffersrc_ctx, frame)) < 0) {
          break;
        }
        // 从滤镜图中获取处理后的帧
        while ((ret = av_buffersink_get_frame(buffersink_ctx, filt_frame)) >= 0) {
          // 将水印后的视频帧送入编码器
          filt_frame->pict_type = AV_PICTURE_TYPE_NONE;
          ret = avcodec_send_frame(enc_ctx, filt_frame);
          if (ret < 0) {
            break;
          }
          AVPacket enc_pkt;
          av_init_packet(&enc_pkt);
          enc_pkt.data = NULL;
          enc_pkt.size = 0;
          while ((ret = avcodec_receive_packet(enc_ctx, &enc_pkt)) >= 0) {
            av_packet_rescale_ts(&enc_pkt, enc_ctx->time_base, out_video_stream->time_base);
            enc_pkt.stream_index = out_video_stream->index;
            ret = av_interleaved_write_frame(ofmt_ctx, &enc_pkt);
            av_packet_unref(&enc_pkt);
            if (ret < 0) {
              break;
            }
          }
          av_frame_unref(filt_frame);
        }
        av_frame_unref(frame);
      }
    }
    av_packet_unref(&packet);
  }

  // 刷出残留的解码和编码数据
  avcodec_send_packet(dec_ctx, NULL);
  while (avcodec_receive_frame(dec_ctx, frame) >= 0) {
    av_buffersrc_add_frame(buffersrc_ctx, frame);
    while (av_buffersink_get_frame(buffersink_ctx, filt_frame) >= 0) {
      filt_frame->pict_type = AV_PICTURE_TYPE_NONE;
      avcodec_send_frame(enc_ctx, filt_frame);
      AVPacket enc_pkt;
      av_init_packet(&enc_pkt);
      enc_pkt.data = NULL;
      enc_pkt.size = 0;
      while (avcodec_receive_packet(enc_ctx, &enc_pkt) >= 0) {
        av_packet_rescale_ts(&enc_pkt, enc_ctx->time_base, out_video_stream->time_base);
        enc_pkt.stream_index = out_video_stream->index;
        av_interleaved_write_frame(ofmt_ctx, &enc_pkt);
        av_packet_unref(&enc_pkt);
      }
      av_frame_unref(filt_frame);
    }
    av_frame_unref(frame);
  }

  // 刷出编码器
  avcodec_send_frame(enc_ctx, NULL);
  while (1) {
    AVPacket enc_pkt;
    av_init_packet(&enc_pkt);
    enc_pkt.data = NULL;
    enc_pkt.size = 0;
    ret = avcodec_receive_packet(enc_ctx, &enc_pkt);
    if (ret == AVERROR_EOF || ret == AVERROR(EAGAIN))
      break;
    if (ret < 0)
      break;
    av_packet_rescale_ts(&enc_pkt, enc_ctx->time_base, out_video_stream->time_base);
    enc_pkt.stream_index = out_video_stream->index;
    av_interleaved_write_frame(ofmt_ctx, &enc_pkt);
    av_packet_unref(&enc_pkt);
  }

  av_write_trailer(ofmt_ctx);

  end_fail:
  {
    char resultMsg[256] = {0};
    if (ret < 0) {
      snprintf(resultMsg, sizeof(resultMsg), "Failed to add watermark, error code: %d", ret);
    } else {
      snprintf(resultMsg, sizeof(resultMsg), "Watermark added successfully, output saved to %s", outputPath);
    }
    // 清理释放分配的资源
    if (filter_graph)
      avfilter_graph_free(&filter_graph);
    if (dec_ctx)
      avcodec_free_context(&dec_ctx);
    if (enc_ctx)
      avcodec_free_context(&enc_ctx);
    if (ifmt_ctx)
      avformat_close_input(&ifmt_ctx);
    if (ofmt_ctx) {
      if (!(ofmt_ctx->oformat->flags & AVFMT_NOFILE))
        avio_closep(&ofmt_ctx->pb);
      avformat_free_context(ofmt_ctx);
    }
    av_frame_free(&frame);
    av_frame_free(&filt_frame);
    if (inputPath)
      (*env)->ReleaseStringUTFChars(env, inputVideoPathJ, inputPath);
    if (outputPath)
      (*env)->ReleaseStringUTFChars(env, outputVideoPathJ, outputPath);
    if (watermarkText)
      (*env)->ReleaseStringUTFChars(env, watermarkTextJ, watermarkText);
    if (fontFile)
      (*env)->ReleaseStringUTFChars(env, fontFileJ, fontFile);
    return (*env)->NewStringUTF(env, resultMsg);
  }
}