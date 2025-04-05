#include "com_litongjava_media_NativeMedia.h"
#include <jni.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#ifdef _WIN32
#include <windows.h>
#endif

// FFmpeg header files (ensure the FFmpeg development environment is properly configured)
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/opt.h>

/**
 * Helper function: Convert FFmpeg error code to a string
 */
static void print_error(const char *msg, int errnum) {
  char errbuf[128] = {0};
  av_strerror(errnum, errbuf, sizeof(errbuf));
  fprintf(stderr, "%s: %s\n", msg, errbuf);
}

/**
 * JNI method implementation:
 * Java declaration:
 *   public static native String splitMp4ToHLS(String playlistUrl, String inputMp4Path, String tsPattern, int sceneIndex, int segmentDuration);
 *
 * Real implementation steps:
 *   1. Open the input MP4 file and retrieve stream information.
 *   2. Allocate an output AVFormatContext using the "hls" muxer, with the output URL specified as playlistUrl.
 *   3. Set HLS options:
 *         - hls_time: segment duration
 *         - start_number: starting segment number (sceneIndex)
 *         - hls_segment_filename: TS segment file naming template (including directory)
 *         - hls_flags=append_list: append if the playlist already exists
 *         - hls_list_size=0: do not limit the playlist length
 *         - hls_playlist_type=event: avoid writing EXT‑X‑ENDLIST to allow future appending
 *   4. Create an output stream for each input stream (using copy mode).
 *   5. Open the output file (if required) and write the header.
 *   6. Read input packets, adjust timestamps, and write to the output to complete the segmentation.
 *   7. Write trailer and release resources.
 *   8. Return a feedback message.
 */
JNIEXPORT jstring JNICALL Java_com_litongjava_media_NativeMedia_splitMp4ToHLS(JNIEnv *env, jclass clazz,
                                                                              jstring playlistUrlJ,
                                                                              jstring inputMp4PathJ, jstring tsPatternJ,
                                                                              jint sceneIndex, jint segmentDuration) {

  int ret = 0;
  AVFormatContext *ifmt_ctx = NULL;
  AVFormatContext *ofmt_ctx = NULL;
  AVDictionary *opts = NULL;
  int *stream_mapping = NULL;
  int stream_mapping_size = 0;
  AVPacket pkt;

  // Retrieve JNI string parameters (all in UTF-8 encoding)
  const char *playlistUrl = (*env)->GetStringUTFChars(env, playlistUrlJ, NULL);
  const char *inputMp4Path = (*env)->GetStringUTFChars(env, inputMp4PathJ, NULL);
  const char *tsPattern = (*env)->GetStringUTFChars(env, tsPatternJ, NULL);

  // Check if the playlist file already contains EXT-X-ENDLIST (appending is not allowed)
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
          (*env)->ReleaseStringUTFChars(env, playlistUrlJ, playlistUrl);
          (*env)->ReleaseStringUTFChars(env, inputMp4PathJ, inputMp4Path);
          (*env)->ReleaseStringUTFChars(env, tsPatternJ, tsPattern);
          return (*env)->NewStringUTF(env, "Playlist already contains #EXT-X-ENDLIST, cannot append");
        }
        free(content);
      }
      fclose(checkFile);
    }
  }

  // Initialize FFmpeg (newer versions do not require av_register_all)
  // Open the input MP4 file
  if ((ret = avformat_open_input(&ifmt_ctx, inputMp4Path, NULL, NULL)) < 0) {
    print_error("Unable to open input file", ret);
    goto end;
  }
  if ((ret = avformat_find_stream_info(ifmt_ctx, NULL)) < 0) {
    print_error("Unable to retrieve stream info", ret);
    goto end;
  }

  // Allocate output context using the hls muxer, with output URL specified as playlistUrl
  ret = avformat_alloc_output_context2(&ofmt_ctx, NULL, "hls", playlistUrl);
  if (ret < 0 || !ofmt_ctx) {
    print_error("Unable to allocate output context", ret);
    goto end;
  }

  // Set HLS options (to be passed to avformat_write_header)
  {
    char seg_time_str[16] = {0};
    snprintf(seg_time_str, sizeof(seg_time_str), "%d", segmentDuration);
    av_dict_set(&opts, "hls_time", seg_time_str, 0);

    char start_num_str[16] = {0};
    snprintf(start_num_str, sizeof(start_num_str), "%d", sceneIndex);
    av_dict_set(&opts, "start_number", start_num_str, 0);

    // Use the provided tsPattern as the TS segment file naming template
    av_dict_set(&opts, "hls_segment_filename", tsPattern, 0);

    // Set append mode, unlimited playlist, and event type (to prevent writing EXT-X-ENDLIST)
    av_dict_set(&opts, "hls_flags", "append_list", 0);
    av_dict_set(&opts, "hls_list_size", "0", 0);
    av_dict_set(&opts, "hls_playlist_type", "event", 0);
  }

  // Create an output stream for each input stream (copy mode)
  stream_mapping_size = ifmt_ctx->nb_streams;
  stream_mapping = av_malloc_array(stream_mapping_size, sizeof(int));
  if (!stream_mapping) {
    ret = AVERROR(ENOMEM);
    goto end;
  }
  for (int i = 0, j = 0; i < ifmt_ctx->nb_streams; i++) {
    AVStream *in_stream = ifmt_ctx->streams[i];
    AVCodecParameters *in_codecpar = in_stream->codecpar;
    // Only copy video, audio, or subtitle streams (filter as needed)
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
    // Set the time base to that of the input stream (to avoid timestamp errors)
    out_stream->time_base = in_stream->time_base;
  }

  // Open the output file (if required by the output format)
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

  // Write the header (the output muxer will generate TS files and update the playlist based on the provided options)
  ret = avformat_write_header(ofmt_ctx, &opts);
  if (ret < 0) {
    print_error("Failed to write header", ret);
    goto end;
  }

  // Start reading packets from the input file and writing them to the output muxer (the hls muxer automatically handles real-time segmentation)
  while (av_read_frame(ifmt_ctx, &pkt) >= 0) {
    if (pkt.stream_index >= ifmt_ctx->nb_streams ||
        stream_mapping[pkt.stream_index] < 0) {
      av_packet_unref(&pkt);
      continue;
    }
    // Retrieve input and output streams
    AVStream *in_stream = ifmt_ctx->streams[pkt.stream_index];
    AVStream *out_stream = ofmt_ctx->streams[stream_mapping[pkt.stream_index]];

    // Convert timestamps
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

  // Note: Not calling av_write_trailer will prevent the muxer from writing EXT-X-ENDLIST, thus allowing subsequent appending
  // av_write_trailer(ofmt_ctx);

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

  // Release JNI strings
  (*env)->ReleaseStringUTFChars(env, playlistUrlJ, playlistUrl);
  (*env)->ReleaseStringUTFChars(env, inputMp4PathJ, inputMp4Path);
  (*env)->ReleaseStringUTFChars(env, tsPatternJ, tsPattern);

  if (ret < 0) {
    return (*env)->NewStringUTF(env, "HLS segmentation failed");
  } else {
    char resultMsg[256] = {0};
    snprintf(resultMsg, sizeof(resultMsg), "HLS segmentation successful: generated new segments and appended to playlist %s", playlistUrl);
    return (*env)->NewStringUTF(env, resultMsg);
  }
}