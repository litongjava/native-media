#include "native_mp3.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
// FFmpeg 头文件
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
#include <libavutil/audio_fifo.h>
#include <libavutil/mathematics.h>
#include <libavutil/common.h>

#ifdef _WIN32

#include <stringapiset.h>

#endif

#include "audio_file_utils.h"

char *convert_to_mp3(const char *input_file, const char *output_file) {
  // 初始化各变量
  AVFormatContext *input_format_context = NULL;
  AVFormatContext *output_format_context = NULL;
  SwrContext *swr_context = NULL;
  AVCodecContext *decoder_context = NULL;
  AVCodecContext *encoder_context = NULL;
  const AVCodec *encoder = NULL;
  const AVCodec *decoder = NULL;
  AVStream *audio_stream = NULL;
  AVPacket *input_packet = NULL;
  AVPacket *output_packet = NULL;
  AVFrame *input_frame = NULL;
  AVFrame *output_frame = NULL;
  AVAudioFifo *fifo = NULL;
  char error_buffer[1024] = {0};
  int ret = 0;
  int audio_stream_index = -1;

  // PTS跟踪变量
  int64_t next_encoding_frame_pts = 0;
  int processing_frame_size_flush = 0;

  // 1. 打开输入文件
  ret = open_input_file_utf8(&input_format_context, input_file);
  if (ret < 0) {
    av_strerror(ret, error_buffer, sizeof(error_buffer));
    snprintf(error_buffer, sizeof(error_buffer), "Error: Could not open input file '%s': %s", input_file, error_buffer);
    goto cleanup;
  }

  // 2. 获取流信息
  if ((ret = avformat_find_stream_info(input_format_context, NULL)) < 0) {
    av_strerror(ret, error_buffer, sizeof(error_buffer));
    snprintf(error_buffer, sizeof(error_buffer), "Error: Could not find stream info: %s", error_buffer);
    goto cleanup;
  }

  // 3. 查找第一个音频流
  for (unsigned int i = 0; i < input_format_context->nb_streams; i++) {
    if (input_format_context->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
      audio_stream_index = i;
      break;
    }
  }
  if (audio_stream_index == -1) {
    snprintf(error_buffer, sizeof(error_buffer), "Error: Could not find audio stream in input file");
    goto cleanup;
  }

  // 4. 设置解码器
  decoder = avcodec_find_decoder(input_format_context->streams[audio_stream_index]->codecpar->codec_id);
  if (!decoder) {
    snprintf(error_buffer, sizeof(error_buffer), "Error: Could not find decoder for codec ID %d",
             input_format_context->streams[audio_stream_index]->codecpar->codec_id);
    goto cleanup;
  }

  decoder_context = avcodec_alloc_context3(decoder);
  if (!decoder_context) {
    snprintf(error_buffer, sizeof(error_buffer), "Error: Could not allocate decoder context");
    goto cleanup;
  }

  if ((ret = avcodec_parameters_to_context(decoder_context,
                                           input_format_context->streams[audio_stream_index]->codecpar)) < 0) {
    av_strerror(ret, error_buffer, sizeof(error_buffer));
    snprintf(error_buffer, sizeof(error_buffer), "Error: Could not copy decoder parameters: %s", error_buffer);
    goto cleanup;
  }

  // 设置解码器 time_base
  decoder_context->time_base = input_format_context->streams[audio_stream_index]->time_base;

  if ((ret = avcodec_open2(decoder_context, decoder, NULL)) < 0) {
    av_strerror(ret, error_buffer, sizeof(error_buffer));
    snprintf(error_buffer, sizeof(error_buffer), "Error: Could not open decoder: %s", error_buffer);
    goto cleanup;
  }

  // 5. 设置编码器和输出格式
  if ((ret = avformat_alloc_output_context2(&output_format_context, NULL, "mp3", output_file)) < 0) {
    av_strerror(ret, error_buffer, sizeof(error_buffer));
    snprintf(error_buffer, sizeof(error_buffer), "Error: Could not allocate output context: %s", error_buffer);
    goto cleanup;
  }

  encoder = avcodec_find_encoder_by_name("libmp3lame");
  if (!encoder) {
    snprintf(error_buffer, sizeof(error_buffer), "Error: Could not find libmp3lame encoder");
    goto cleanup;
  }

  audio_stream = avformat_new_stream(output_format_context, NULL);
  if (!audio_stream) {
    snprintf(error_buffer, sizeof(error_buffer), "Error: Could not create new audio stream");
    goto cleanup;
  }

  encoder_context = avcodec_alloc_context3(encoder);
  if (!encoder_context) {
    snprintf(error_buffer, sizeof(error_buffer), "Error: Could not allocate encoder context");
    goto cleanup;
  }

  // 设置编码器参数
  encoder_context->sample_rate = decoder_context->sample_rate;
  encoder_context->bit_rate = 128000;
  encoder_context->sample_fmt = AV_SAMPLE_FMT_S16P; // libmp3lame 通常使用 s16p

#if LIBAVUTIL_VERSION_MAJOR < 57
  encoder_context->channels = decoder_context->channels;
  encoder_context->channel_layout = decoder_context->channel_layout;

  // 修复声道布局问题
  if (encoder_context->channels == 1) {
    encoder_context->channel_layout = AV_CH_LAYOUT_MONO;
  } else if (encoder_context->channels == 2) {
    encoder_context->channel_layout = AV_CH_LAYOUT_STEREO;
  }
#else
  av_channel_layout_copy(&encoder_context->ch_layout, &decoder_context->ch_layout);

  // 修复声道布局问题
  if (encoder_context->ch_layout.nb_channels == 1) {
    av_channel_layout_uninit(&encoder_context->ch_layout);
    av_channel_layout_default(&encoder_context->ch_layout, 1);
  } else if (encoder_context->ch_layout.nb_channels == 2) {
    av_channel_layout_uninit(&encoder_context->ch_layout);
    av_channel_layout_default(&encoder_context->ch_layout, 2);
  }
#endif

  if (output_format_context->oformat->flags & AVFMT_GLOBALHEADER) {
    encoder_context->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
  }

  // 打开编码器
  if ((ret = avcodec_open2(encoder_context, encoder, NULL)) < 0) {
    av_strerror(ret, error_buffer, sizeof(error_buffer));
    snprintf(error_buffer, sizeof(error_buffer), "Error: Could not open encoder: %s", error_buffer);
    goto cleanup;
  }

  if ((ret = avcodec_parameters_from_context(audio_stream->codecpar, encoder_context)) < 0) {
    av_strerror(ret, error_buffer, sizeof(error_buffer));
    snprintf(error_buffer, sizeof(error_buffer), "Error: Could not copy encoder parameters: %s", error_buffer);
    goto cleanup;
  }

  // 设置输出流 time_base
  audio_stream->time_base = (AVRational) {1, encoder_context->sample_rate};

  // 6. 初始化 SwrContext
  swr_context = swr_alloc();
  if (!swr_context) {
    snprintf(error_buffer, sizeof(error_buffer), "Error: Could not allocate resampler context");
    goto cleanup;
  }

#if LIBAVUTIL_VERSION_MAJOR < 57
  av_opt_set_int(swr_context, "in_channel_layout", decoder_context->channel_layout, 0);
  av_opt_set_int(swr_context, "in_channel_count", decoder_context->channels, 0);
  av_opt_set_int(swr_context, "out_channel_layout", encoder_context->channel_layout, 0);
  av_opt_set_int(swr_context, "out_channel_count", encoder_context->channels, 0);
#else
  av_opt_set_chlayout(swr_context, "in_chlayout", &decoder_context->ch_layout, 0);
  av_opt_set_chlayout(swr_context, "out_chlayout", &encoder_context->ch_layout, 0);
#endif

  av_opt_set_int(swr_context, "in_sample_rate", decoder_context->sample_rate, 0);
  av_opt_set_int(swr_context, "out_sample_rate", encoder_context->sample_rate, 0);
  av_opt_set_sample_fmt(swr_context, "in_sample_fmt", decoder_context->sample_fmt, 0);
  av_opt_set_sample_fmt(swr_context, "out_sample_fmt", encoder_context->sample_fmt, 0);

  if ((ret = swr_init(swr_context)) < 0) {
    av_strerror(ret, error_buffer, sizeof(error_buffer));
    snprintf(error_buffer, sizeof(error_buffer), "Error: Could not initialize resampler: %s", error_buffer);
    goto cleanup;
  }

  // 7. 打开输出文件
  if (!(output_format_context->oformat->flags & AVFMT_NOFILE)) {
    ret = open_output_file_utf8(&output_format_context->pb, output_file);
    if (ret < 0) {
      av_strerror(ret, error_buffer, sizeof(error_buffer));
      snprintf(error_buffer, sizeof(error_buffer), "Error: Could not open output file '%s': %s", output_file,
               error_buffer);
      goto cleanup;
    }
  }

  // 8. 写输出文件头
  if ((ret = avformat_write_header(output_format_context, NULL)) < 0) {
    av_strerror(ret, error_buffer, sizeof(error_buffer));
    snprintf(error_buffer, sizeof(error_buffer), "Error: Could not write output header: %s", error_buffer);
    goto cleanup;
  }

  // 9. 初始化 FIFO
#define FIFO_INIT_SIZE 10 * (encoder_context->frame_size > 0 ? encoder_context->frame_size : 1152)
#if LIBAVUTIL_VERSION_MAJOR < 57
  fifo = av_audio_fifo_alloc(encoder_context->sample_fmt, encoder_context->channels, FIFO_INIT_SIZE);
#else
  fifo = av_audio_fifo_alloc(encoder_context->sample_fmt, encoder_context->ch_layout.nb_channels, FIFO_INIT_SIZE);
#endif
  if (!fifo) {
    snprintf(error_buffer, sizeof(error_buffer), "Error: Could not allocate FIFO");
    goto cleanup;
  }

  // 10. 分配数据包与帧
  input_packet = av_packet_alloc();
  output_packet = av_packet_alloc();
  input_frame = av_frame_alloc();
  output_frame = av_frame_alloc();
  if (!input_packet || !output_packet || !input_frame || !output_frame) {
    snprintf(error_buffer, sizeof(error_buffer), "Error: Could not allocate packet or frame");
    goto cleanup;
  }

  // 为重采样后的数据准备临时缓冲区，使用较大的缓冲区避免数据丢失
  output_frame->format = encoder_context->sample_fmt;
#if LIBAVUTIL_VERSION_MAJOR < 57
  output_frame->channel_layout = encoder_context->channel_layout;
  output_frame->channels = encoder_context->channels;
#else
  av_channel_layout_copy(&output_frame->ch_layout, &encoder_context->ch_layout);
#endif

  // 使用较大的缓冲区以确保能容纳所有重采样后的数据
  output_frame->nb_samples = 8192; // 增大缓冲区
  if ((ret = av_frame_get_buffer(output_frame, 0)) < 0) {
    av_strerror(ret, error_buffer, sizeof(error_buffer));
    snprintf(error_buffer, sizeof(error_buffer), "Error: Could not allocate output frame buffer: %s", error_buffer);
    goto cleanup;
  }

  // 初始化PTS
  next_encoding_frame_pts = 0;

  // 11. 主处理循环
  while (1) {
    AVPacket *pkt_to_send = NULL;

    // 读取数据包
    ret = av_read_frame(input_format_context, input_packet);
    if (ret >= 0 && input_packet->stream_index == audio_stream_index) {
      pkt_to_send = input_packet;
    } else if (ret == AVERROR_EOF) {
      pkt_to_send = NULL; // EOF，准备flush
    } else if (ret < 0) {
      av_strerror(ret, error_buffer, sizeof(error_buffer));
      snprintf(error_buffer, sizeof(error_buffer), "Error reading frame: %s", error_buffer);
      goto cleanup;
    } else {
      // 不是音频包，跳过
      av_packet_unref(input_packet);
      continue;
    }

    // 发送包到解码器
    ret = avcodec_send_packet(decoder_context, pkt_to_send);
    if (ret < 0) {
      av_strerror(ret, error_buffer, sizeof(error_buffer));
      snprintf(error_buffer, sizeof(error_buffer), "Error sending packet to decoder: %s", error_buffer);
      if (pkt_to_send) av_packet_unref(input_packet);
      goto cleanup;
    }

    if (pkt_to_send) av_packet_unref(input_packet);

    // 从解码器接收帧
    while (1) {
      ret = avcodec_receive_frame(decoder_context, input_frame);
      if (ret == AVERROR(EAGAIN)) {
        break; // 需要更多数据
      } else if (ret == AVERROR_EOF) {
        goto flush_encoder; // 解码完成，开始flush编码器
      } else if (ret < 0) {
        av_strerror(ret, error_buffer, sizeof(error_buffer));
        snprintf(error_buffer, sizeof(error_buffer), "Error receiving frame from decoder: %s", error_buffer);
        goto cleanup;
      }

      // 确保输出帧可写
      if ((ret = av_frame_make_writable(output_frame)) < 0) {
        av_strerror(ret, error_buffer, sizeof(error_buffer));
        snprintf(error_buffer, sizeof(error_buffer), "Error making frame writable: %s", error_buffer);
        av_frame_unref(input_frame);
        goto cleanup;
      }

      // 重采样转换
      int nb_samples_converted = swr_convert(swr_context,
                                             output_frame->data, output_frame->nb_samples,
                                             (const uint8_t **) input_frame->data, input_frame->nb_samples);
      if (nb_samples_converted < 0) {
        av_strerror(nb_samples_converted, error_buffer, sizeof(error_buffer));
        snprintf(error_buffer, sizeof(error_buffer), "Error converting audio: %s", error_buffer);
        av_frame_unref(input_frame);
        goto cleanup;
      }

      // 将重采样后的数据写入FIFO
      if (nb_samples_converted > 0) {
        if (av_audio_fifo_write(fifo, (void **) output_frame->data, nb_samples_converted) < nb_samples_converted) {
          snprintf(error_buffer, sizeof(error_buffer), "Error: Could not write data to FIFO");
          av_frame_unref(input_frame);
          goto cleanup;
        }
      }

      // 处理FIFO中的数据
      const int processing_frame_size = encoder_context->frame_size > 0 ? encoder_context->frame_size : 1152;
      while (av_audio_fifo_size(fifo) >= processing_frame_size) {
        AVFrame *enc_frame = av_frame_alloc();
        if (!enc_frame) {
          snprintf(error_buffer, sizeof(error_buffer), "Error: Could not allocate encoding frame");
          av_frame_unref(input_frame);
          goto cleanup;
        }

        enc_frame->nb_samples = processing_frame_size;
        enc_frame->format = encoder_context->sample_fmt;
#if LIBAVUTIL_VERSION_MAJOR < 57
        enc_frame->channel_layout = encoder_context->channel_layout;
        enc_frame->channels = encoder_context->channels;
#else
        av_channel_layout_copy(&enc_frame->ch_layout, &encoder_context->ch_layout);
#endif

        if ((ret = av_frame_get_buffer(enc_frame, 0)) < 0) {
          av_strerror(ret, error_buffer, sizeof(error_buffer));
          snprintf(error_buffer, sizeof(error_buffer), "Error: Could not allocate buffer for encoding frame: %s",
                   error_buffer);
          av_frame_free(&enc_frame);
          av_frame_unref(input_frame);
          goto cleanup;
        }

        // 从FIFO读取数据
        if (av_audio_fifo_read(fifo, (void **) enc_frame->data, processing_frame_size) < processing_frame_size) {
          snprintf(error_buffer, sizeof(error_buffer), "Error: Could not read data from FIFO");
          av_frame_free(&enc_frame);
          av_frame_unref(input_frame);
          goto cleanup;
        }

        // 设置PTS
        enc_frame->pts = next_encoding_frame_pts;
        next_encoding_frame_pts += processing_frame_size;

        // 发送帧到编码器
        ret = avcodec_send_frame(encoder_context, enc_frame);
        if (ret < 0) {
          av_strerror(ret, error_buffer, sizeof(error_buffer));
          snprintf(error_buffer, sizeof(error_buffer), "Error sending frame to encoder: %s", error_buffer);
          av_frame_free(&enc_frame);
          av_frame_unref(input_frame);
          goto cleanup;
        }
        av_frame_free(&enc_frame);

        // 接收编码后的包
        while (1) {
          ret = avcodec_receive_packet(encoder_context, output_packet);
          if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
          } else if (ret < 0) {
            av_strerror(ret, error_buffer, sizeof(error_buffer));
            snprintf(error_buffer, sizeof(error_buffer), "Error receiving packet from encoder: %s", error_buffer);
            av_frame_unref(input_frame);
            goto cleanup;
          }

          output_packet->stream_index = 0;
          av_packet_rescale_ts(output_packet,
                               encoder_context->time_base,
                               output_format_context->streams[0]->time_base);

          ret = av_interleaved_write_frame(output_format_context, output_packet);
          if (ret < 0) {
            av_strerror(ret, error_buffer, sizeof(error_buffer));
            snprintf(error_buffer, sizeof(error_buffer), "Error writing packet: %s", error_buffer);
            av_packet_unref(output_packet);
            av_frame_unref(input_frame);
            goto cleanup;
          }
          av_packet_unref(output_packet);
        }
      }

      av_frame_unref(input_frame);
    }

    // 如果是EOF，跳出主循环
    if (ret == AVERROR_EOF) {
      break;
    }
  }

  flush_encoder:
  // 处理FIFO中剩余的数据
  processing_frame_size_flush = encoder_context->frame_size > 0 ? encoder_context->frame_size : 1152;

  // 首先处理剩余的SwrContext缓冲数据
  while (1) {
    if ((ret = av_frame_make_writable(output_frame)) < 0) {
      av_strerror(ret, error_buffer, sizeof(error_buffer));
      snprintf(error_buffer, sizeof(error_buffer), "Error making frame writable during flush: %s", error_buffer);
      goto cleanup;
    }

    // 冲刷SwrContext中剩余的数据
    int nb_samples_converted = swr_convert(swr_context,
                                           output_frame->data, output_frame->nb_samples,
                                           NULL, 0);
    if (nb_samples_converted < 0) {
      av_strerror(nb_samples_converted, error_buffer, sizeof(error_buffer));
      snprintf(error_buffer, sizeof(error_buffer), "Error flushing resampler: %s", error_buffer);
      goto cleanup;
    }

    if (nb_samples_converted == 0) {
      break; // 没有更多数据了
    }

    // 将冲刷出的数据写入FIFO
    if (av_audio_fifo_write(fifo, (void **) output_frame->data, nb_samples_converted) < nb_samples_converted) {
      snprintf(error_buffer, sizeof(error_buffer), "Error: Could not write flushed data to FIFO");
      goto cleanup;
    }
  }

  // 处理FIFO中所有剩余数据
  while (av_audio_fifo_size(fifo) > 0) {
    int remaining_samples = av_audio_fifo_size(fifo);
    int samples_to_read = FFMIN(remaining_samples, processing_frame_size_flush);

    AVFrame *enc_frame = av_frame_alloc();
    if (!enc_frame) {
      snprintf(error_buffer, sizeof(error_buffer), "Error: Could not allocate final encoding frame");
      goto cleanup;
    }

    enc_frame->nb_samples = samples_to_read;
    enc_frame->format = encoder_context->sample_fmt;
#if LIBAVUTIL_VERSION_MAJOR < 57
    enc_frame->channel_layout = encoder_context->channel_layout;
    enc_frame->channels = encoder_context->channels;
#else
    av_channel_layout_copy(&enc_frame->ch_layout, &encoder_context->ch_layout);
#endif

    if ((ret = av_frame_get_buffer(enc_frame, 0)) < 0) {
      av_strerror(ret, error_buffer, sizeof(error_buffer));
      snprintf(error_buffer, sizeof(error_buffer), "Error: Could not allocate buffer for final frame: %s",
               error_buffer);
      av_frame_free(&enc_frame);
      goto cleanup;
    }

    if (av_audio_fifo_read(fifo, (void **) enc_frame->data, samples_to_read) < samples_to_read) {
      snprintf(error_buffer, sizeof(error_buffer), "Error: Could not read final data from FIFO");
      av_frame_free(&enc_frame);
      goto cleanup;
    }

    enc_frame->pts = next_encoding_frame_pts;
    next_encoding_frame_pts += samples_to_read;

    ret = avcodec_send_frame(encoder_context, enc_frame);
    if (ret < 0 && ret != AVERROR_EOF) {
      av_strerror(ret, error_buffer, sizeof(error_buffer));
      snprintf(error_buffer, sizeof(error_buffer), "Error sending final frame to encoder: %s", error_buffer);
      av_frame_free(&enc_frame);
      goto cleanup;
    }
    av_frame_free(&enc_frame);

    // 接收编码后的包
    while (1) {
      ret = avcodec_receive_packet(encoder_context, output_packet);
      if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
        break;
      } else if (ret < 0) {
        av_strerror(ret, error_buffer, sizeof(error_buffer));
        snprintf(error_buffer, sizeof(error_buffer), "Error receiving final packet from encoder: %s", error_buffer);
        goto cleanup;
      }

      output_packet->stream_index = 0;
      av_packet_rescale_ts(output_packet,
                           encoder_context->time_base,
                           output_format_context->streams[0]->time_base);

      ret = av_interleaved_write_frame(output_format_context, output_packet);
      if (ret < 0) {
        av_strerror(ret, error_buffer, sizeof(error_buffer));
        snprintf(error_buffer, sizeof(error_buffer), "Error writing final packet: %s", error_buffer);
        av_packet_unref(output_packet);
        goto cleanup;
      }
      av_packet_unref(output_packet);
    }
  }

  // 冲刷编码器
  ret = avcodec_send_frame(encoder_context, NULL);
  if (ret < 0 && ret != AVERROR_EOF) {
    av_strerror(ret, error_buffer, sizeof(error_buffer));
    snprintf(error_buffer, sizeof(error_buffer), "Error flushing encoder: %s", error_buffer);
    goto cleanup;
  }

  while (1) {
    ret = avcodec_receive_packet(encoder_context, output_packet);
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
      break;
    } else if (ret < 0) {
      av_strerror(ret, error_buffer, sizeof(error_buffer));
      snprintf(error_buffer, sizeof(error_buffer), "Error flushing encoder (receive): %s", error_buffer);
      goto cleanup;
    }

    output_packet->stream_index = 0;
    av_packet_rescale_ts(output_packet,
                         encoder_context->time_base,
                         output_format_context->streams[0]->time_base);

    ret = av_interleaved_write_frame(output_format_context, output_packet);
    if (ret < 0) {
      av_strerror(ret, error_buffer, sizeof(error_buffer));
      snprintf(error_buffer, sizeof(error_buffer), "Error writing flushed packet: %s", error_buffer);
      av_packet_unref(output_packet);
      goto cleanup;
    }
    av_packet_unref(output_packet);
  }

  // 写文件尾
  if ((ret = av_write_trailer(output_format_context)) < 0) {
    av_strerror(ret, error_buffer, sizeof(error_buffer));
    snprintf(error_buffer, sizeof(error_buffer), "Error writing trailer: %s", error_buffer);
    goto cleanup;
  }

  // 成功时返回输出文件路径
  strncpy(error_buffer, output_file, sizeof(error_buffer) - 1);
  error_buffer[sizeof(error_buffer) - 1] = '\0';

  cleanup:
  if (input_frame) av_frame_free(&input_frame);
  if (output_frame) av_frame_free(&output_frame);
  if (input_packet) av_packet_free(&input_packet);
  if (output_packet) av_packet_free(&output_packet);
  if (decoder_context) avcodec_free_context(&decoder_context);
  if (encoder_context) avcodec_free_context(&encoder_context);
  if (swr_context) swr_free(&swr_context);
  if (fifo) av_audio_fifo_free(fifo);
  if (input_format_context) avformat_close_input(&input_format_context);
  if (output_format_context) {
    if (!(output_format_context->oformat->flags & AVFMT_NOFILE) && output_format_context->pb) {
      avio_closep(&output_format_context->pb);
    }
    avformat_free_context(output_format_context);
  }

  // 返回结果
  char *result = malloc(strlen(error_buffer) + 1);
  if (result) {
    strcpy(result, error_buffer);
  }
  return result;
}