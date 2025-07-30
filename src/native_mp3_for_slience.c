#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
// FFmpeg 头文件
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>

#include <libavutil/audio_fifo.h>
#include <libavutil/mathematics.h>

#ifdef _WIN32

#include <stringapiset.h>

#endif

#include "native_mp3.h"
#include "audio_file_utils.h"

#define SILENCE_THRESHOLD_LINEAR 0.0001 // noise=0.0001
#define SILENCE_DURATION_SEC 0.1        // duration=0.1

// 只负责生成静音数据并写入 fifo
static int insert_silence_into_fifo(AVAudioFifo *fifo, int sample_rate, enum AVSampleFormat sample_fmt, int channels,
                                    double duration_sec);

// 计算单个平面 (channel) 的 RMS (Root Mean Square)
static double calculate_rms(const uint8_t *samples, int nb_samples, enum AVSampleFormat format) {
  double sum = 0.0;
  switch (format) {
    case AV_SAMPLE_FMT_S16P:
    case AV_SAMPLE_FMT_S16: {
      const int16_t *s = (const int16_t *) samples;
      for (int i = 0; i < nb_samples; i++) {
        double sample = s[i] / (double) INT16_MAX; // Convert to -1.0 to 1.0
        sum += sample * sample;
      }
      break;
    }
    case AV_SAMPLE_FMT_S32P:
    case AV_SAMPLE_FMT_S32: {
      const int32_t *s = (const int32_t *) samples;
      for (int i = 0; i < nb_samples; i++) {
        double sample = s[i] / (double) INT32_MAX;
        sum += sample * sample;
      }
      break;
    }
    case AV_SAMPLE_FMT_FLTP:
    case AV_SAMPLE_FMT_FLT: {
      const float *s = (const float *) samples;
      for (int i = 0; i < nb_samples; i++) {
        sum += s[i] * s[i];
      }
      break;
    }
    case AV_SAMPLE_FMT_DBLP:
    case AV_SAMPLE_FMT_DBL: {
      const double *s = (const double *) samples;
      for (int i = 0; i < nb_samples; i++) {
        sum += s[i] * s[i];
      }
      break;
    }
      // 可以根据需要添加更多格式
    default:
      // Unsupported format for this simple RMS calculation
      // fprintf(stderr, "Unsupported sample format for RMS: %d\n", format);
      return -1.0;
  }
  if (nb_samples > 0) {
    return sqrt(sum / nb_samples);
  }
  return 0.0;
}

// 检测帧是否为静音 (基于所有通道的平均 RMS)
static int is_silence_frame(AVFrame *frame, double threshold_linear) {
  double total_rms = 0.0;
  int num_channels = 0;
#if LIBAVUTIL_VERSION_MAJOR >= 57
  num_channels = frame->ch_layout.nb_channels;
#else
  num_channels = frame->channels;
#endif
  if (num_channels <= 0 || frame->nb_samples <= 0) {
    return 0; // Cannot determine, assume not silence
  }
  // 假设输出格式是 planar (如 AV_SAMPLE_FMT_S16P)
  if (av_sample_fmt_is_planar(frame->format)) {
    for (int ch = 0; ch < num_channels; ch++) {
      double rms = calculate_rms(frame->data[ch], frame->nb_samples, frame->format);
      if (rms < 0) return 0; // Error in RMS calculation, assume not silence
      total_rms += rms;
    }
  } else {
    // For packed formats, calculate RMS across the entire data block
    // This is a simplification and might need adjustment
    double rms = calculate_rms(frame->data[0], frame->nb_samples * num_channels, frame->format);
    if (rms < 0) return 0;
    total_rms = rms; // Use the overall RMS for packed data
    // Or calculate per channel equivalent if needed
  }
  double avg_rms = total_rms / num_channels;
  // printf("DEBUG: Avg RMS: %f\n", avg_rms); // Debug print
  return avg_rms < threshold_linear;
}


char *convert_to_mp3_for_silence(const char *input_file, const char *output_file, double insertion_silence_duration) {
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
  // 移除了 insertion_fifo
  char error_buffer[1024] = {0};
  int ret = 0;
  int audio_stream_index = -1;
  // --- 修改 1: 移除 next_pts，使用更精确的 PTS追踪 ---
  // int64_t next_pts = 0; // 移除这个变量
  int64_t total_output_samples_written = 0; // 追踪已写入 FIFO 的总输出样本数
  int64_t next_encoding_frame_pts = 0;      // 追踪下一个编码帧的PTS (简化方法)
  // --- 新增: 静音检测状态变量 (调整以匹配 duration 逻辑) ---
  int is_currently_silent = 0;
  int64_t silence_start_sample = -1;
  int64_t accumulated_silence_samples = 0; // 累积的静音样本数
  double silence_threshold_linear = SILENCE_THRESHOLD_LINEAR;
  double silence_duration_sec = SILENCE_DURATION_SEC;
  int64_t silence_duration_samples_threshold = 0; // 将在 encoder_context 初始化后设置
  int silence_segments_detected = 0; // --- 新增: 记录检测到的静音段数量 ---
  // --- 新增结束 ---
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
  // 设置解码器 time_base (很重要!)
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
  // --- 修改: 明确指定编码器名称 ---
  encoder = avcodec_find_encoder_by_name("libmp3lame");
  // --- 修改结束 ---
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

  // libmp3lame 通常支持 s16p, s16, fltp, flt
  // 我们优先选择 planar 格式，因为它通常效率更高
  const enum AVSampleFormat *sample_fmts = encoder->sample_fmts;
  encoder_context->sample_fmt = AV_SAMPLE_FMT_NONE;
  if (sample_fmts) {
    for (int i = 0; sample_fmts[i] != AV_SAMPLE_FMT_NONE; i++) {
      if (sample_fmts[i] == AV_SAMPLE_FMT_S16P) {
        encoder_context->sample_fmt = AV_SAMPLE_FMT_S16P;
        break;
      }
    }
    // 如果没有找到 s16p，找第一个支持的
    if (encoder_context->sample_fmt == AV_SAMPLE_FMT_NONE) {
      encoder_context->sample_fmt = sample_fmts[0];
    }
  } else {
    // 如果编码器没有声明支持的格式列表，使用默认的 s16p
    encoder_context->sample_fmt = AV_SAMPLE_FMT_S16P;
  }
  if (encoder_context->sample_fmt == AV_SAMPLE_FMT_NONE) {
    snprintf(error_buffer, sizeof(error_buffer), "Error: Could not determine suitable sample format for encoder");
    goto cleanup;
  }

#if LIBAVUTIL_VERSION_MAJOR < 57
  encoder_context->channels = decoder_context->channels;
  encoder_context->channel_layout = decoder_context->channel_layout;
#else
  av_channel_layout_copy(&encoder_context->ch_layout, &decoder_context->ch_layout);
#endif
  if (output_format_context->oformat->flags & AVFMT_GLOBALHEADER) {
    encoder_context->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
  }
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
  // 在 encoder_context 初始化后设置阈值 ---
  silence_duration_samples_threshold = (int64_t)(silence_duration_sec * encoder_context->sample_rate);

  // 6. 设置重采样器
  swr_context = swr_alloc();
  if (!swr_context) {
    snprintf(error_buffer, sizeof(error_buffer), "Error: Could not allocate resampler context");
    goto cleanup;
  }
#if LIBAVUTIL_VERSION_MAJOR < 57
  av_opt_set_int(swr_context, "in_channel_layout", decoder_context->channel_layout, 0);
  av_opt_set_int(swr_context, "out_channel_layout", encoder_context->channel_layout, 0);
  av_opt_set_int(swr_context, "in_channel_count", decoder_context->channels, 0);
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
#if LIBAVUTIL_VERSION_MAJOR < 57
  fifo = av_audio_fifo_alloc(encoder_context->sample_fmt, encoder_context->channels, 1);
#else
  fifo = av_audio_fifo_alloc(encoder_context->sample_fmt, encoder_context->ch_layout.nb_channels, 1);
#endif
  //移除 insertion_fifo 的检查 ---
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
  // 为重采样后的数据准备 output_frame（临时缓冲区）
  output_frame->format = encoder_context->sample_fmt;
#if LIBAVUTIL_VERSION_MAJOR < 57
  output_frame->channel_layout = encoder_context->channel_layout;
  output_frame->channels = encoder_context->channels;
#else
  av_channel_layout_copy(&output_frame->ch_layout, &encoder_context->ch_layout);
#endif
  // 设置一个默认采样数（实际将由 swr_convert 返回）
  output_frame->nb_samples = encoder_context->frame_size > 0 ? encoder_context->frame_size : 1152;
  if ((ret = av_frame_get_buffer(output_frame, 0)) < 0) {
    av_strerror(ret, error_buffer, sizeof(error_buffer));
    snprintf(error_buffer, sizeof(error_buffer), "Error: Could not allocate output frame buffer: %s", error_buffer);
    goto cleanup;
  }
  // --- 初始化新的 PTS 变量 ---
  total_output_samples_written = 0;
  next_encoding_frame_pts = 0; // 初始化
  // --- 初始化静音检测变量 ---
  is_currently_silent = 0;
  silence_start_sample = -1;
  accumulated_silence_samples = 0;
  silence_segments_detected = 0; // --- 新增: 初始化计数器 ---
  // --- 解码和编码主循环 ---
  // 11. 读取输入包
  while (1) {
    AVPacket *pkt_to_send = NULL;
    if (!(ret = av_read_frame(input_format_context, input_packet)) &&
        input_packet->stream_index == audio_stream_index) {
      pkt_to_send = input_packet;
    } else if (ret == AVERROR_EOF) {
      // 文件读取完毕，发送 NULL 包冲刷解码器
      pkt_to_send = NULL;
    } else if (ret < 0) {
      av_strerror(ret, error_buffer, sizeof(error_buffer));
      snprintf(error_buffer, sizeof(error_buffer), "Error reading frame: %s", error_buffer);
      goto cleanup;
    } else {
      // 不是音频包，跳过
      av_packet_unref(input_packet);
      continue;
    }
    ret = avcodec_send_packet(decoder_context, pkt_to_send);
    if (ret < 0) {
      av_strerror(ret, error_buffer, sizeof(error_buffer));
      snprintf(error_buffer, sizeof(error_buffer), "Error sending packet to decoder: %s", error_buffer);
      if (pkt_to_send) av_packet_unref(input_packet);
      goto cleanup;
    }
    if (pkt_to_send) av_packet_unref(input_packet); // 发送成功后立即释放
    // 12. 从解码器接收帧
    while (1) {
      ret = avcodec_receive_frame(decoder_context, input_frame);
      if (ret == AVERROR(EAGAIN)) {
        // 需要更多数据包才能解码
        break;
      } else if (ret == AVERROR_EOF) {
        // 解码器已冲刷完毕
        goto flush_encoder; // 跳出读包循环，进入编码器冲刷阶段
      } else if (ret < 0) {
        av_strerror(ret, error_buffer, sizeof(error_buffer));
        snprintf(error_buffer, sizeof(error_buffer), "Error receiving frame from decoder: %s", error_buffer);
        goto cleanup;
      }
      // --- 获取并转换输入帧的 PTS ---
      int64_t input_pts = input_frame->pts;
      if (input_pts == AV_NOPTS_VALUE) {
        // 如果没有PTS，回退到基于样本计数的估算 (这可能不够准确，但比完全不用好)
        // 使用当前已写入的样本数作为PTS
        input_pts = av_rescale_q(total_output_samples_written,
                                 (AVRational) {1, encoder_context->sample_rate},
                                 input_format_context->streams[audio_stream_index]->time_base);
      }
      // 将输入PTS转换到编码器的时间基 (通常是 1/sample_rate)
      int64_t output_pts = av_rescale_q_rnd(input_pts,
                                            input_format_context->streams[audio_stream_index]->time_base,
                                            (AVRational) {1, encoder_context->sample_rate},
                                            AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
      if (output_pts == AV_NOPTS_VALUE) {
        // 如果转换后还是无效，回退到基于样本计数
        output_pts = total_output_samples_written;
      }

      // 13. 确保 output_frame 可写
      if ((ret = av_frame_make_writable(output_frame)) < 0) {
        av_strerror(ret, error_buffer, sizeof(error_buffer));
        snprintf(error_buffer, sizeof(error_buffer), "Error making frame writable: %s", error_buffer);
        av_frame_unref(input_frame);
        goto cleanup;
      }
      // 14. 重采样转换
      int nb_samples_converted = swr_convert(swr_context,
                                             output_frame->data, output_frame->nb_samples,
                                             (const uint8_t **) input_frame->data, input_frame->nb_samples);
      if (nb_samples_converted < 0) {
        av_strerror(nb_samples_converted, error_buffer, sizeof(error_buffer));
        snprintf(error_buffer, sizeof(error_buffer), "Error converting audio: %s", error_buffer);
        av_frame_unref(input_frame);
        goto cleanup;
      }
      output_frame->nb_samples = nb_samples_converted; // 更新实际转换的样本数
      // 静音检测逻辑 (匹配 ffmpeg to_mp3_for_insert_silence duration 逻辑)

      int is_frame_silent = is_silence_frame(output_frame, silence_threshold_linear);
      if (is_frame_silent) {
        // 当前帧是静音
        if (!is_currently_silent) {
          // 刚刚进入静音状态
          is_currently_silent = 1;
          silence_start_sample = total_output_samples_written; // 记录开始位置
          accumulated_silence_samples = nb_samples_converted;  // 初始化累积计数 (或 0?)
          // printf("DEBUG: Silence START candidate at sample %ld\n", silence_start_sample);
        } else {
          // 已经在静音状态中，继续累积
          accumulated_silence_samples += nb_samples_converted;
        }
      } else {
        // 当前帧不是静音
        if (is_currently_silent) {
          // 刚刚退出静音状态
          // 检查累积的静音时间是否满足 duration 阈值
          // accumulated_silence_samples 已经包含了当前静音段的样本数
          if (accumulated_silence_samples >= silence_duration_samples_threshold) {
            // 满足 duration 条件，报告静音事件
            // 静音结束点是上一帧的末尾 (即当前帧的开始)
            int64_t silence_end_sample = total_output_samples_written; // 结束于当前帧开始前
            double silence_start_sec = (double) silence_start_sample / encoder_context->sample_rate;
            double silence_end_sec = (double) silence_end_sample / encoder_context->sample_rate;
            double silence_duration_sec_reported = silence_end_sec - silence_start_sec;
            // --- 修改: 增加静音段计数器 ---
            silence_segments_detected++;
            // --- 修改: 打印静音片段信息，并根据条件插入静音 ---
            printf("[to_mp3_for_insert_silence @ %p] silence_start: %.6f\n", (void *) 0x12345678, silence_start_sec);
            printf("[to_mp3_for_insert_silence @ %p] silence_end: %.6f | silence_duration: %.6f\n", (void *) 0x12345678,
                   silence_end_sec, silence_duration_sec_reported);
            // 插入静音逻辑 (忽略第一个静音段) ---
            if (silence_segments_detected > 1) {
              printf("[INFO @ %p] Inserting %.3f seconds of silence at %.6f\n", (void *) 0x12345678,
                     insertion_silence_duration, silence_start_sec);
              // --- 关键修改: 直接将静音数据写入主 fifo ---
              ret = insert_silence_into_fifo(fifo,
                                             encoder_context->sample_rate,
                                             encoder_context->sample_fmt,
#if LIBAVUTIL_VERSION_MAJOR < 57
                encoder_context->channels,
#else
                                             encoder_context->ch_layout.nb_channels,
#endif
                                             insertion_silence_duration);
              if (ret < 0) {
                snprintf(error_buffer, sizeof(error_buffer), "Error inserting silence into main FIFO");
                av_frame_unref(input_frame);
                goto cleanup;
              }
              // 更新追踪的样本数和PTS
              int64_t inserted_samples = (int64_t)(insertion_silence_duration * encoder_context->sample_rate);
              total_output_samples_written += inserted_samples;
              next_encoding_frame_pts += inserted_samples; // Simplified PTS update
            }

            // --- 打印结束 ---
          } else {
            // 静音时间太短，不报告
            // printf("DEBUG: Silence segment too short (%.6f s < %.6f s)\n",
            //        (double)accumulated_silence_samples / encoder_context->sample_rate,
            //        silence_duration_sec);
          }
          // 退出静音状态
          is_currently_silent = 0;
          silence_start_sample = -1;
          accumulated_silence_samples = 0;
        }
        // 如果本来就不在静音状态，则无需操作
      }

      // 更新总输出样本数 (在写入FIFO之前) ---
      total_output_samples_written += nb_samples_converted;

      // 15. 将转换后的样本写入主 FIFO
      if (av_audio_fifo_write(fifo, (void **) output_frame->data, nb_samples_converted) < nb_samples_converted) {
        snprintf(error_buffer, sizeof(error_buffer), "Error: Could not write data to main FIFO");
        av_frame_unref(input_frame);
        goto cleanup;
      }

      av_frame_unref(input_frame);

      // 17. 当主 FIFO 中样本足够构成一帧时，从主 FIFO 中读取固定数量样本送编码器
      // --- 关键修改: 只处理完整的 frame_size 帧 ---
      while (av_audio_fifo_size(fifo) >= encoder_context->frame_size) {
        AVFrame *enc_frame = av_frame_alloc();
        if (!enc_frame) {
          snprintf(error_buffer, sizeof(error_buffer), "Error: Could not allocate main encoding frame");
          goto cleanup;
        }
        enc_frame->nb_samples = encoder_context->frame_size;
        enc_frame->format = encoder_context->sample_fmt;
#if LIBAVUTIL_VERSION_MAJOR < 57
        enc_frame->channel_layout = encoder_context->channel_layout;
        enc_frame->channels = encoder_context->channels;
#else
        av_channel_layout_copy(&enc_frame->ch_layout, &encoder_context->ch_layout);
#endif
        // --- 关键修改: 使用 align=0 允许分配标准大小的帧缓冲区 ---
        if ((ret = av_frame_get_buffer(enc_frame, 0)) < 0) {
          av_strerror(ret, error_buffer, sizeof(error_buffer));
          snprintf(error_buffer, sizeof(error_buffer), "Error: Could not allocate buffer for main encoding frame: %s",
                   error_buffer);
          av_frame_free(&enc_frame);
          goto cleanup;
        }
        if (av_audio_fifo_read(fifo, (void **) enc_frame->data, encoder_context->frame_size) <
            encoder_context->frame_size) {
          snprintf(error_buffer, sizeof(error_buffer), "Error: Could not read data from main FIFO");
          av_frame_free(&enc_frame);
          goto cleanup;
        }
        // --- 修改 5: 为编码帧分配正确的 PTS (使用简化追踪方法) ---
        enc_frame->pts = next_encoding_frame_pts;
        next_encoding_frame_pts += encoder_context->frame_size; // 更新下一个帧的PTS
        // --- 修改 5 结束 ---
        ret = avcodec_send_frame(encoder_context, enc_frame);
        if (ret < 0) {
          av_strerror(ret, error_buffer, sizeof(error_buffer));
          snprintf(error_buffer, sizeof(error_buffer), "Error sending main frame to encoder: %s", error_buffer);
          av_frame_free(&enc_frame);
          goto cleanup;
        }
        av_frame_free(&enc_frame);
        // 18. 从编码器接收数据包并写入输出文件
        while (1) {
          ret = avcodec_receive_packet(encoder_context, output_packet);
          if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
          } else if (ret < 0) {
            av_strerror(ret, error_buffer, sizeof(error_buffer));
            snprintf(error_buffer, sizeof(error_buffer), "Error receiving packet from encoder: %s", error_buffer);
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
            goto cleanup;
          }
          av_packet_unref(output_packet);
        }
      }
    }
  }

  flush_encoder:
  // 处理文件末尾可能存在的静音 (如果满足 duration 条件) ---
  if (is_currently_silent && accumulated_silence_samples >= silence_duration_samples_threshold) {
    // 文件以满足 duration 条件的静音结束
    int64_t silence_end_sample = total_output_samples_written; // 结束于文件末尾
    double silence_start_sec = (double) silence_start_sample / encoder_context->sample_rate;
    double silence_end_sec = (double) silence_end_sample / encoder_context->sample_rate;
    double silence_duration_sec_reported = silence_end_sec - silence_start_sec;
    // 增加静音段计数器 (即使在文件末尾) ---
    silence_segments_detected++;
    // --- 打印文件末尾的静音片段信息 ---
    printf("[to_mp3_for_insert_silence @ %p] silence_start: %.6f\n", (void *) 0x12345678, silence_start_sec);
    printf("[to_mp3_for_insert_silence @ %p] silence_end: %.6f | silence_duration: %.6f\n", (void *) 0x12345678,
           silence_end_sec,
           silence_duration_sec_reported);
    // 插入静音逻辑 (忽略第一个静音段，即使在文件末尾) ---
    if (silence_segments_detected > 1) {
      printf("[INFO @ %p] Inserting %.3f seconds of silence at %.6f (end of file)\n", (void *) 0x12345678,
             insertion_silence_duration, silence_start_sec);
      // 直接将静音数据写入主 fifo ---
      ret = insert_silence_into_fifo(fifo,
                                     encoder_context->sample_rate,
                                     encoder_context->sample_fmt,
#if LIBAVUTIL_VERSION_MAJOR < 57
        encoder_context->channels,
#else
                                     encoder_context->ch_layout.nb_channels,
#endif
                                     insertion_silence_duration);
      if (ret < 0) {
        snprintf(error_buffer, sizeof(error_buffer), "Error inserting silence into main FIFO (end of file)");
        goto cleanup;
      }
      // 更新追踪的样本数和PTS
      int64_t inserted_samples = (int64_t)(insertion_silence_duration * encoder_context->sample_rate);
      total_output_samples_written += inserted_samples;
      next_encoding_frame_pts += inserted_samples; // Simplified PTS update
    }

  }

  // 正确处理主 FIFO 中剩余样本 ---
  // 处理主 FIFO 中剩余不足一帧的样本 (这将是最后一帧)
  while (av_audio_fifo_size(fifo) > 0) {
    int remaining_samples = av_audio_fifo_size(fifo);
    // 读取所有剩余样本，即使少于一帧
    int samples_to_read = remaining_samples;
    AVFrame *enc_frame = av_frame_alloc();
    if (!enc_frame) {
      snprintf(error_buffer, sizeof(error_buffer), "Error: Could not allocate final main encoding frame");
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
    // Use av_frame_get_buffer with align=0 for potentially non-standard nb_samples
    // 使用 align=0 允许分配非标准大小的帧 ---
    if ((ret = av_frame_get_buffer(enc_frame, 0)) < 0) {
      av_strerror(ret, error_buffer, sizeof(error_buffer));
      snprintf(error_buffer, sizeof(error_buffer), "Error: Could not allocate buffer for final main frame: %s",
               error_buffer);
      av_frame_free(&enc_frame);
      goto cleanup;
    }
    if (av_audio_fifo_read(fifo, (void **) enc_frame->data, samples_to_read) < samples_to_read) {
      snprintf(error_buffer, sizeof(error_buffer), "Error: Could not read final data from main FIFO");
      av_frame_free(&enc_frame);
      goto cleanup;
    }
    // 为最后的帧分配正确的 PTS ---
    enc_frame->pts = next_encoding_frame_pts; // 使用追踪到的PTS
    next_encoding_frame_pts += samples_to_read; // 更新 (虽然循环会结束，但保持逻辑一致)
    // 发送最后一帧到编码器
    ret = avcodec_send_frame(encoder_context, enc_frame);
    if (ret < 0) {
      av_strerror(ret, error_buffer, sizeof(error_buffer));
      // 区分错误类型 ---
      if (ret == AVERROR(EINVAL)) {
        // EINVAL 通常表示帧大小不合规，但对于最后一帧，某些编码器可能仍能处理
        // 或者是编码器状态问题。我们记录警告但尝试继续冲刷。
        fprintf(stderr,
                "Warning: Encoder reported EINVAL for final frame (size %d). This might be expected for the last frame. Proceeding to flush.\n",
                samples_to_read);
        // 不要立即 goto cleanup，继续执行 flush
      } else {
        snprintf(error_buffer, sizeof(error_buffer), "Error sending final main frame to encoder: %s", error_buffer);
        av_frame_free(&enc_frame);
        goto cleanup;
      }
    }
    av_frame_free(&enc_frame); // 无论是否成功发送，都释放帧

    // 尝试接收可能的编码数据包 ---
    while (1) {
      ret = avcodec_receive_packet(encoder_context, output_packet);
      if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
        break;
      else if (ret < 0) {
        av_strerror(ret, error_buffer, sizeof(error_buffer));
        // 对于最后一帧，接收错误可能也是预期的，记录警告 ---
        fprintf(stderr, "Warning: Error receiving final packet from encoder: %s\n", error_buffer);
        break; // 不中断主流程
      }
      if (ret == 0) { // 成功接收到包
        output_packet->stream_index = 0;
        av_packet_rescale_ts(output_packet,
                             encoder_context->time_base,
                             output_format_context->streams[0]->time_base);
        ret = av_interleaved_write_frame(output_format_context, output_packet);
        if (ret < 0) {
          av_strerror(ret, error_buffer, sizeof(error_buffer));
          fprintf(stderr, "Warning: Error writing final packet: %s\n", error_buffer);
        }
        av_packet_unref(output_packet);
      }
    }
  }

  // 20. 冲刷编码器（发送 NULL 帧）
  // 确保冲刷逻辑健壮 ---
  ret = avcodec_send_frame(encoder_context, NULL);
  if (ret < 0) {
    av_strerror(ret, error_buffer, sizeof(error_buffer));
    fprintf(stderr, "Warning: Error sending flush frame (NULL) to encoder: %s\n", error_buffer);
    // 不立即 goto cleanup, 继续尝试接收剩余数据包
  }

  // 接收冲刷过程中产生的所有数据包
  while (1) {
    ret = avcodec_receive_packet(encoder_context, output_packet);
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
      break;
    else if (ret < 0) {
      av_strerror(ret, error_buffer, sizeof(error_buffer));
      // 冲刷接收错误通常不是致命错误，记录警告 ---
      fprintf(stderr, "Warning: Error receiving flushed packet from encoder: %s\n", error_buffer);
      break; // 不中断主流程
    }
    if (ret == 0) { // 成功接收到包
      output_packet->stream_index = 0;
      av_packet_rescale_ts(output_packet,
                           encoder_context->time_base,
                           output_format_context->streams[0]->time_base);
      ret = av_interleaved_write_frame(output_format_context, output_packet);
      if (ret < 0) {
        av_strerror(ret, error_buffer, sizeof(error_buffer));
        fprintf(stderr, "Warning: Error writing flushed packet: %s\n", error_buffer);
      }
      av_packet_unref(output_packet);
    }
  }

  // 21. 写文件尾
  if ((ret = av_write_trailer(output_format_context)) < 0) {
    av_strerror(ret, error_buffer, sizeof(error_buffer));
    snprintf(error_buffer, sizeof(error_buffer), "Error writing trailer: %s", error_buffer);
    goto cleanup;
  }

  // 成功时将输出文件路径作为结果返回
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
  // if (insertion_fifo) av_audio_fifo_free(insertion_fifo); // --- 移除 ---
  if (input_format_context) avformat_close_input(&input_format_context);
  if (output_format_context) {
    if (!(output_format_context->oformat->flags & AVFMT_NOFILE) && output_format_context->pb) {
      avio_closep(&output_format_context->pb);
    }
    avformat_free_context(output_format_context);
  }

  // Return a copy of the error message or output path
  char *result = malloc(strlen(error_buffer) + 1);
  if (result) {
    strcpy(result, error_buffer);
  }
  return result;
}

// 它现在只负责将指定时长的静音样本放入给定的 fifo
static int insert_silence_into_fifo(AVAudioFifo *fifo, int sample_rate, enum AVSampleFormat sample_fmt, int channels,
                                    double duration_sec) {
  if (!fifo) {
    return AVERROR(EINVAL);
  }
  int64_t samples_to_insert = (int64_t)(duration_sec * sample_rate);
  if (samples_to_insert <= 0) {
    return 0; // Nothing to insert
  }
  int bytes_per_sample = av_get_bytes_per_sample(sample_fmt);
  if (bytes_per_sample <= 0) {
    return AVERROR(EINVAL);
  }
  uint8_t **silence_data = NULL;
  int is_planar = av_sample_fmt_is_planar(sample_fmt);
  int num_planes = is_planar ? channels : 1;
  silence_data = av_calloc(num_planes, sizeof(*silence_data));
  if (!silence_data) {
    return AVERROR(ENOMEM);
  }
  for (int i = 0; i < num_planes; i++) {
    silence_data[i] = av_malloc(samples_to_insert * bytes_per_sample);
    if (!silence_data[i]) {
      for (int j = 0; j < i; j++) {
        av_free(silence_data[j]);
      }
      av_free(silence_data);
      return AVERROR(ENOMEM);
    }
    // 填充静音数据 (0)
    // 注意：对于有符号整数格式，0 代表静音。对于浮点格式，0.0 也代表静音。
    memset(silence_data[i], 0, samples_to_insert * bytes_per_sample);
  }
  // 将静音数据写入 FIFO
  int written_samples = av_audio_fifo_write(fifo, (void **) silence_data, samples_to_insert);
  if (written_samples != samples_to_insert) {
    // Cleanup on error
    for (int i = 0; i < num_planes; i++) {
      av_free(silence_data[i]);
    }
    av_free(silence_data);
    return AVERROR(EIO);
  }
  // Cleanup temporary buffers
  for (int i = 0; i < num_planes; i++) {
    av_free(silence_data[i]);
  }
  av_free(silence_data);
  return 0; // Success
}