#include "com_litongjava_media_NativeMedia.h"
#include <jni.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// FFmpeg headers
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
#include <stringapiset.h>

JNIEXPORT jstring JNICALL Java_com_litongjava_media_NativeMedia_mp4ToMp3(JNIEnv *env, jclass clazz, jstring inputPath) {
  // Convert Java string to C string
  const char *input_file = (*env)->GetStringUTFChars(env, inputPath, NULL);
  if (!input_file) {
    return (*env)->NewStringUTF(env, "Error: Failed to get input file path");
  }

  // Create output filename (replace .mp4 with .mp3)
  char *output_file = NULL;
  size_t input_len = strlen(input_file);
  if (input_len > 4 && strncmp(input_file + input_len - 4, ".mp4", 4) == 0) {
    output_file = (char *) malloc(input_len + 1);
    if (!output_file) {
      (*env)->ReleaseStringUTFChars(env, inputPath, input_file);
      return (*env)->NewStringUTF(env, "Error: Memory allocation failed");
    }
    strcpy(output_file, input_file);
    strcpy(output_file + input_len - 4, ".mp3");
  } else {
    output_file = (char *) malloc(input_len + 5);
    if (!output_file) {
      (*env)->ReleaseStringUTFChars(env, inputPath, input_file);
      return (*env)->NewStringUTF(env, "Error: Memory allocation failed");
    }
    strcpy(output_file, input_file);
    strcat(output_file, ".mp3");
  }

  // Initialize variables
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
  char error_buffer[1024] = {0};
  int ret = 0;
  int audio_stream_index = -1;
  jstring result = NULL;

  // Open input file
#ifdef _WIN32
  int wlen = MultiByteToWideChar(CP_UTF8, 0, input_file, -1, NULL, 0);
  wchar_t *winput_file = malloc(wlen * sizeof(wchar_t));
  if (winput_file) {
    MultiByteToWideChar(CP_UTF8, 0, input_file, -1, winput_file, wlen);
    int len = WideCharToMultiByte(CP_ACP, 0, winput_file, -1, NULL, 0, NULL, NULL);
    char *local_input_file = malloc(len);
    if (local_input_file) {
      WideCharToMultiByte(CP_ACP, 0, winput_file, -1, local_input_file, len, NULL, NULL);
      ret = avformat_open_input(&input_format_context, local_input_file, NULL, NULL);
      free(local_input_file);
    } else {
      ret = -1;
    }
    free(winput_file);
  } else {
    ret = -1;
  }
#else
  ret = avformat_open_input(&input_format_context, input_file, NULL, NULL);
#endif

  if (ret < 0) {
    av_strerror(ret, error_buffer, sizeof(error_buffer));
    snprintf(error_buffer, sizeof(error_buffer), "Error: Could not open input file: %s", error_buffer);
    goto cleanup;
  }


  // Find stream info
  if ((ret = avformat_find_stream_info(input_format_context, NULL)) < 0) {
    av_strerror(ret, error_buffer, sizeof(error_buffer));
    snprintf(error_buffer, sizeof(error_buffer), "Error: Could not find stream info: %s", error_buffer);
    goto cleanup;
  }

  // Find the first audio stream
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

  // Find decoder for the audio stream
  decoder = avcodec_find_decoder(input_format_context->streams[audio_stream_index]->codecpar->codec_id);
  if (!decoder) {
    snprintf(error_buffer, sizeof(error_buffer), "Error: Could not find decoder");
    goto cleanup;
  }

  // Allocate decoder context
  decoder_context = avcodec_alloc_context3(decoder);
  if (!decoder_context) {
    snprintf(error_buffer, sizeof(error_buffer), "Error: Could not allocate decoder context");
    goto cleanup;
  }

  // Copy codec parameters to decoder context
  if ((ret = avcodec_parameters_to_context(decoder_context,
                                           input_format_context->streams[audio_stream_index]->codecpar)) < 0) {
    av_strerror(ret, error_buffer, sizeof(error_buffer));
    snprintf(error_buffer, sizeof(error_buffer), "Error: Could not copy decoder parameters: %s", error_buffer);
    goto cleanup;
  }

  // Open decoder
  if ((ret = avcodec_open2(decoder_context, decoder, NULL)) < 0) {
    av_strerror(ret, error_buffer, sizeof(error_buffer));
    snprintf(error_buffer, sizeof(error_buffer), "Error: Could not open decoder: %s", error_buffer);
    goto cleanup;
  }

  // Create output format context
  if ((ret = avformat_alloc_output_context2(&output_format_context, NULL, "mp3", output_file)) < 0) {
    av_strerror(ret, error_buffer, sizeof(error_buffer));
    snprintf(error_buffer, sizeof(error_buffer), "Error: Could not allocate output context: %s", error_buffer);
    goto cleanup;
  }

  // Find the MP3 encoder
//  encoder = avcodec_find_encoder(AV_CODEC_ID_MP3);
//  if (!encoder) {
//    snprintf(error_buffer, sizeof(error_buffer), "Error: Could not find MP3 encoder");
//    goto cleanup;
//  }

  encoder = avcodec_find_encoder_by_name("libmp3lame");
  if (!encoder) {
    snprintf(error_buffer, sizeof(error_buffer), "Error: Could not find libmp3lame encoder");
    goto cleanup;
  }

  // Create a new audio stream in the output file
  audio_stream = avformat_new_stream(output_format_context, NULL);
  if (!audio_stream) {
    snprintf(error_buffer, sizeof(error_buffer), "Error: Could not create new audio stream");
    goto cleanup;
  }

  // Allocate encoder context
  encoder_context = avcodec_alloc_context3(encoder);
  if (!encoder_context) {
    snprintf(error_buffer, sizeof(error_buffer), "Error: Could not allocate encoder context");
    goto cleanup;
  }

  // Set encoder parameters
  encoder_context->sample_rate = decoder_context->sample_rate;
  encoder_context->bit_rate = 128000;  // 128kbps bitrate
  encoder_context->sample_fmt = AV_SAMPLE_FMT_S16P; // MP3 encoder typically uses s16p
  // encoder_context->sample_fmt = AV_SAMPLE_FMT_S16; // MP3 encoder supports s16




  // Set up channel layout based on FFmpeg version
#if LIBAVUTIL_VERSION_MAJOR < 57
  encoder_context->channels = 2;
    encoder_context->channel_layout = AV_CH_LAYOUT_STEREO;
#else
  av_channel_layout_default(&encoder_context->ch_layout, 2);
#endif

  // Some container formats (like MP4) require global headers
  if (output_format_context->oformat->flags & AVFMT_GLOBALHEADER) {
    encoder_context->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
  }

  // Open encoder
  if ((ret = avcodec_open2(encoder_context, encoder, NULL)) < 0) {
    av_strerror(ret, error_buffer, sizeof(error_buffer));
    snprintf(error_buffer, sizeof(error_buffer), "Error: Could not open encoder: %s", error_buffer);
    goto cleanup;
  }

  // Copy encoder parameters to stream
  if ((ret = avcodec_parameters_from_context(audio_stream->codecpar, encoder_context)) < 0) {
    av_strerror(ret, error_buffer, sizeof(error_buffer));
    snprintf(error_buffer, sizeof(error_buffer), "Error: Could not copy encoder parameters: %s", error_buffer);
    goto cleanup;
  }

  // Set stream timebase
  audio_stream->time_base = (AVRational) {1, encoder_context->sample_rate};

  // Create resampler context
  swr_context = swr_alloc();
  if (!swr_context) {
    snprintf(error_buffer, sizeof(error_buffer), "Error: Could not allocate resampler context");
    goto cleanup;
  }

  // Set resampler options
#if LIBAVUTIL_VERSION_MAJOR < 57
  // Set input channel layout
    av_opt_set_int(swr_context, "in_channel_layout", decoder_context->channel_layout, 0);
    // Set output channel layout
    av_opt_set_int(swr_context, "out_channel_layout", encoder_context->channel_layout, 0);
    // Set input & output channels
    av_opt_set_int(swr_context, "in_channel_count", decoder_context->channels, 0);
    av_opt_set_int(swr_context, "out_channel_count", encoder_context->channels, 0);
#else
  // Set input channel layout
  av_opt_set_chlayout(swr_context, "in_chlayout", &decoder_context->ch_layout, 0);
  // Set output channel layout
  av_opt_set_chlayout(swr_context, "out_chlayout", &encoder_context->ch_layout, 0);
#endif

  // Set input & output sample rates
  av_opt_set_int(swr_context, "in_sample_rate", decoder_context->sample_rate, 0);
  av_opt_set_int(swr_context, "out_sample_rate", encoder_context->sample_rate, 0);

  // Set input & output sample formats
  av_opt_set_sample_fmt(swr_context, "in_sample_fmt", decoder_context->sample_fmt, 0);
  av_opt_set_sample_fmt(swr_context, "out_sample_fmt", encoder_context->sample_fmt, 0);

  // Initialize resampler
  if ((ret = swr_init(swr_context)) < 0) {
    av_strerror(ret, error_buffer, sizeof(error_buffer));
    snprintf(error_buffer, sizeof(error_buffer), "Error: Could not initialize resampler: %s", error_buffer);
    goto cleanup;
  }

  // Open output file for writing
  if (!(output_format_context->oformat->flags & AVFMT_NOFILE)) {
#ifdef _WIN32
    int wlen = MultiByteToWideChar(CP_UTF8, 0, output_file, -1, NULL, 0);
    wchar_t *woutput_file = malloc(wlen * sizeof(wchar_t));
    if (woutput_file) {
      MultiByteToWideChar(CP_UTF8, 0, output_file, -1, woutput_file, wlen);
      int len = WideCharToMultiByte(CP_ACP, 0, woutput_file, -1, NULL, 0, NULL, NULL);
      char *local_output_file = malloc(len);
      if (local_output_file) {
        WideCharToMultiByte(CP_ACP, 0, woutput_file, -1, local_output_file, len, NULL, NULL);
        ret = avio_open(&output_format_context->pb, local_output_file, AVIO_FLAG_WRITE);
        free(local_output_file);
      } else {
        ret = -1;
      }
      free(woutput_file);
    } else {
      ret = -1;
    }
#else
    ret = avio_open(&output_format_context->pb, output_file, AVIO_FLAG_WRITE);
#endif

    if (ret < 0) {
      av_strerror(ret, error_buffer, sizeof(error_buffer));
      snprintf(error_buffer, sizeof(error_buffer), "Error: Could not open output file: %s", error_buffer);
      goto cleanup;
    }

  }

  // Write file header
  if ((ret = avformat_write_header(output_format_context, NULL)) < 0) {
    av_strerror(ret, error_buffer, sizeof(error_buffer));
    snprintf(error_buffer, sizeof(error_buffer), "Error: Could not write output header: %s", error_buffer);
    goto cleanup;
  }

  // Allocate packets
  input_packet = av_packet_alloc();
  output_packet = av_packet_alloc();
  if (!input_packet || !output_packet) {
    snprintf(error_buffer, sizeof(error_buffer), "Error: Could not allocate packet");
    goto cleanup;
  }

  // Allocate frames
  input_frame = av_frame_alloc();
  output_frame = av_frame_alloc();
  if (!input_frame || !output_frame) {
    snprintf(error_buffer, sizeof(error_buffer), "Error: Could not allocate frame");
    goto cleanup;
  }

  // Set output frame properties
  if (encoder_context->frame_size > 0) {
    output_frame->nb_samples = encoder_context->frame_size;
  } else {
    output_frame->nb_samples = 1152;
  }


  output_frame->format = encoder_context->sample_fmt;
#if LIBAVUTIL_VERSION_MAJOR < 57
  output_frame->channel_layout = encoder_context->channel_layout;
    output_frame->channels = encoder_context->channels;
#else
  av_channel_layout_copy(&output_frame->ch_layout, &encoder_context->ch_layout);
#endif

  // Allocate buffer for output frame
  if ((ret = av_frame_get_buffer(output_frame, 0)) < 0) {
    av_strerror(ret, error_buffer, sizeof(error_buffer));
    snprintf(error_buffer, sizeof(error_buffer), "Error: Could not allocate output frame buffer: %s", error_buffer);
    goto cleanup;
  }

  // Read packets from input file
  while (av_read_frame(input_format_context, input_packet) >= 0) {
    // Process only audio stream
    if (input_packet->stream_index == audio_stream_index) {
      // Send packet to decoder
      ret = avcodec_send_packet(decoder_context, input_packet);
      if (ret < 0) {
        av_strerror(ret, error_buffer, sizeof(error_buffer));
        snprintf(error_buffer, sizeof(error_buffer), "Error sending packet to decoder: %s", error_buffer);
        goto cleanup;
      }

      // Receive frames from decoder
      while (1) {
        ret = avcodec_receive_frame(decoder_context, input_frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
          break;
        } else if (ret < 0) {
          av_strerror(ret, error_buffer, sizeof(error_buffer));
          snprintf(error_buffer, sizeof(error_buffer), "Error receiving frame from decoder: %s", error_buffer);
          goto cleanup;
        }

        // Make sure output frame is writable
        if ((ret = av_frame_make_writable(output_frame)) < 0) {
          av_strerror(ret, error_buffer, sizeof(error_buffer));
          snprintf(error_buffer, sizeof(error_buffer), "Error making frame writable: %s", error_buffer);
          goto cleanup;
        }

        // Convert audio samples
        int nb_samples_converted = swr_convert(swr_context,
                                               output_frame->data, output_frame->nb_samples,
                                               (const uint8_t **) input_frame->data, input_frame->nb_samples);
        if (nb_samples_converted < 0) {
          av_strerror(nb_samples_converted, error_buffer, sizeof(error_buffer));
          snprintf(error_buffer, sizeof(error_buffer), "Error converting audio: %s", error_buffer);
          goto cleanup;
        }
        output_frame->nb_samples = nb_samples_converted;

        // Set PTS for the output frame
        output_frame->pts = av_rescale_q(input_frame->pts,
                                         input_format_context->streams[audio_stream_index]->time_base,
                                         encoder_context->time_base);

        // Send frame to encoder
        ret = avcodec_send_frame(encoder_context, output_frame);
        if (ret < 0) {
          av_strerror(ret, error_buffer, sizeof(error_buffer));
          snprintf(error_buffer, sizeof(error_buffer), "Error sending frame to encoder: %s", error_buffer);
          goto cleanup;
        }

        // Receive encoded packets
        while (1) {
          ret = avcodec_receive_packet(encoder_context, output_packet);
          if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
          } else if (ret < 0) {
            av_strerror(ret, error_buffer, sizeof(error_buffer));
            snprintf(error_buffer, sizeof(error_buffer), "Error receiving packet from encoder: %s", error_buffer);
            goto cleanup;
          }

          // Set stream index and rescale timestamps
          output_packet->stream_index = 0;
          av_packet_rescale_ts(output_packet,
                               encoder_context->time_base,
                               output_format_context->streams[0]->time_base);

          // Write packet to output file
          ret = av_interleaved_write_frame(output_format_context, output_packet);
          if (ret < 0) {
            av_strerror(ret, error_buffer, sizeof(error_buffer));
            snprintf(error_buffer, sizeof(error_buffer), "Error writing packet: %s", error_buffer);
            goto cleanup;
          }
        }

        av_frame_unref(input_frame);
      }
    }
    av_packet_unref(input_packet);
  }

  // Flush encoder
  avcodec_send_frame(encoder_context, NULL);
  while (1) {
    ret = avcodec_receive_packet(encoder_context, output_packet);
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
      break;
    } else if (ret < 0) {
      av_strerror(ret, error_buffer, sizeof(error_buffer));
      snprintf(error_buffer, sizeof(error_buffer), "Error flushing encoder: %s", error_buffer);
      goto cleanup;
    }

    // Set stream index and rescale timestamps
    output_packet->stream_index = 0;
    av_packet_rescale_ts(output_packet,
                         encoder_context->time_base,
                         output_format_context->streams[0]->time_base);

    // Write packet to output file
    ret = av_interleaved_write_frame(output_format_context, output_packet);
    if (ret < 0) {
      av_strerror(ret, error_buffer, sizeof(error_buffer));
      snprintf(error_buffer, sizeof(error_buffer), "Error writing flushed packet: %s", error_buffer);
      goto cleanup;
    }
  }

  // Write file trailer
  if ((ret = av_write_trailer(output_format_context)) < 0) {
    av_strerror(ret, error_buffer, sizeof(error_buffer));
    snprintf(error_buffer, sizeof(error_buffer), "Error writing trailer: %s", error_buffer);
    goto cleanup;
  }

  // Success! Set the result to the output file path
  strncpy(error_buffer, output_file, sizeof(error_buffer) - 1);
  error_buffer[sizeof(error_buffer) - 1] = '\0';

  cleanup:
  // Free all allocated resources
  if (input_frame) av_frame_free(&input_frame);
  if (output_frame) av_frame_free(&output_frame);
  if (input_packet) av_packet_free(&input_packet);
  if (output_packet) av_packet_free(&output_packet);
  if (decoder_context) avcodec_free_context(&decoder_context);
  if (encoder_context) avcodec_free_context(&encoder_context);

  if (swr_context) swr_free(&swr_context);

  if (input_format_context) avformat_close_input(&input_format_context);

  if (output_format_context) {
    if (!(output_format_context->oformat->flags & AVFMT_NOFILE) && output_format_context->pb) {
      avio_closep(&output_format_context->pb);
    }
    avformat_free_context(output_format_context);
  }

  // Create Java string for result
  result = (*env)->NewStringUTF(env, error_buffer);

  // Release C strings
  (*env)->ReleaseStringUTFChars(env, inputPath, input_file);
  if (output_file) free(output_file);

  return result;
}