#include "com_litongjava_media_NativeMedia.h" // Replace with your actual JNI header file name
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

// Compatibility for av_channel_layout_copy
#ifndef av_channel_layout_copy

inline int av_channel_layout_copy(AVChannelLayout *dst, const AVChannelLayout *src) {
  if (!dst || !src) return AVERROR(EINVAL);
  memcpy(dst, src, sizeof(*dst));
  dst->opaque = NULL; // Opaque handling removed for simplicity/compatibility
  return 0;
}

#endif // av_channel_layout_copy

/**
 * Persistent HLS Session Structure
 */
typedef struct {
  AVFormatContext *ofmt_ctx;
  int segment_duration;
  int start_number;
  int header_written;
  char *ts_pattern;
  int64_t *last_dts_written; // Array to store last DTS *successfully written* for each stream
  int nb_streams_allocated;
} HlsSession;

// (print_error function remains the same)
static void print_error(const char *msg, int errnum) {
  char errbuf[AV_ERROR_MAX_STRING_SIZE] = {0};
  av_strerror(errnum, errbuf, sizeof(errbuf));
  fprintf(stderr, "%s: %s (%d)\n", msg, errbuf, errnum);
}

// (ensure_stream_tracking function - MODIFIED INITIALIZATION)
static int ensure_stream_tracking(HlsSession *session, int nb_streams) {
  if (!session) return AVERROR(EINVAL);
  if (nb_streams <= 0) return 0;
  if (nb_streams > session->nb_streams_allocated) {
    size_t new_size = nb_streams * sizeof(int64_t);
    int64_t *new_ptr = (int64_t *) realloc(session->last_dts_written, new_size);
    if (!new_ptr) {
      fprintf(stderr, "Failed to reallocate memory for stream DTS tracking (%d streams)\n", nb_streams);
      return AVERROR(ENOMEM);
    }
    for (int i = session->nb_streams_allocated; i < nb_streams; i++) {
      // Initialize to AV_NOPTS_VALUE to indicate no DTS written yet.
      new_ptr[i] = AV_NOPTS_VALUE; // <--- MODIFIED HERE
    }
    session->last_dts_written = new_ptr;
    session->nb_streams_allocated = nb_streams;
  }
  return 0;
}


// --- Helper Function to get the next start DTS --- (MODIFIED LOGIC)
// Finds the maximum DTS written across all streams so far.
// Returns 0 if no packets have been written yet.
static int64_t get_next_start_dts(HlsSession *session) {
  if (!session || !session->last_dts_written || session->nb_streams_allocated == 0) {
    return 0; // Start at 0 if no streams or tracking yet
  }
  int64_t max_dts = AV_NOPTS_VALUE;
  for (int i = 0; i < session->nb_streams_allocated; ++i) {
    if (session->last_dts_written[i] != AV_NOPTS_VALUE) {
      // Found a valid DTS, compare it with the current max
      if (max_dts == AV_NOPTS_VALUE || session->last_dts_written[i] > max_dts) {
        max_dts = session->last_dts_written[i];
      }
    }
  }
  // If no packets have been written yet (all are AV_NOPTS_VALUE), start at 0.
  // Otherwise, start at the next DTS value after the maximum found.
  return (max_dts == AV_NOPTS_VALUE) ? 0 : (max_dts + 1);
}


/*
 * Class:     com_litongjava_media_NativeMedia
 * Method:    initPersistentHls
 * Signature: (Ljava/lang/String;Ljava/lang/String;II)J
 */
JNIEXPORT jlong JNICALL Java_com_litongjava_media_NativeMedia_initPersistentHls
  (JNIEnv *env, jclass clazz, jstring playlistUrlJ, jstring tsPatternJ, jint startNumber, jint segDuration) {
  int ret = 0;
  const char *playlistUrl = NULL;
  const char *tsPattern = NULL;
  HlsSession *session = NULL;

  playlistUrl = (*env)->GetStringUTFChars(env, playlistUrlJ, NULL);
  if (!playlistUrl) {
    ret = -1;
    goto init_end;
  }
  tsPattern = (*env)->GetStringUTFChars(env, tsPatternJ, NULL);
  if (!tsPattern) {
    ret = -1;
    goto init_end;
  }

  session = (HlsSession *) calloc(1, sizeof(HlsSession));
  if (!session) {
    ret = AVERROR(ENOMEM);
    goto init_end;
  }

  // Initialize session fields
  session->segment_duration = segDuration;
  session->start_number = startNumber;
  session->header_written = 0;
  session->last_dts_written = NULL; // Will be allocated by ensure_stream_tracking
  session->nb_streams_allocated = 0;
  session->ts_pattern = strdup(tsPattern);
  if (!session->ts_pattern) {
    ret = AVERROR(ENOMEM);
    goto init_end;
  }

  ret = avformat_alloc_output_context2(&session->ofmt_ctx, NULL, "hls", playlistUrl);
  if (ret < 0 || !session->ofmt_ctx) {
    print_error("Unable to allocate output context", ret < 0 ? ret : AVERROR_UNKNOWN);
    goto init_end;
  }

  if (!(session->ofmt_ctx->oformat->flags & AVFMT_NOFILE)) {
    ret = avio_open(&session->ofmt_ctx->pb, playlistUrl, AVIO_FLAG_WRITE);
    if (ret < 0) {
      print_error("Unable to open output file", ret);
      avformat_free_context(session->ofmt_ctx);
      session->ofmt_ctx = NULL;
      goto init_end;
    }
  }

  init_end:
  if (playlistUrl) (*env)->ReleaseStringUTFChars(env, playlistUrlJ, playlistUrl);
  if (tsPattern) (*env)->ReleaseStringUTFChars(env, tsPatternJ, tsPattern);
  if (ret < 0 && session) {
    free(session->ts_pattern);
    free(session->last_dts_written); // Free tracking array if allocated
    // ofmt_ctx handled above
    free(session);
    session = NULL; // Ensure NULL is returned on failure
  }
  return (jlong) (uintptr_t) session;
}

/*
 * Class:     com_litongjava_media_NativeMedia
 * Method:    appendMp4Segment
 * Signature: (JLjava/lang/String;)Ljava/lang/String;
 */
JNIEXPORT jstring JNICALL Java_com_litongjava_media_NativeMedia_appendMp4Segment
  (JNIEnv *env, jclass clazz, jlong sessionPtr, jstring inputFilePathJ) {
  int ret = 0;
  AVFormatContext *ifmt_ctx = NULL;
  AVPacket pkt;
  const char *inputFilePath = NULL;
  int *stream_mapping = NULL;
  jstring result_string = NULL;
  HlsSession *session = NULL;
  int64_t dts_offset = 0; // Offset to add to this segment's packets
  int64_t first_packet_dts_in_segment_rescaled = AV_NOPTS_VALUE; // DTS of the first packet from input *after rescaling*

  if (!sessionPtr) return (*env)->NewStringUTF(env, "Invalid session pointer");
  session = (HlsSession *) (uintptr_t) sessionPtr;
  if (!session->ofmt_ctx) return (*env)->NewStringUTF(env, "Invalid session: Output context is NULL");

  inputFilePath = (*env)->GetStringUTFChars(env, inputFilePathJ, NULL);
  if (!inputFilePath) return (*env)->NewStringUTF(env, "Failed to get input file path string");

  ret = avformat_open_input(&ifmt_ctx, inputFilePath, NULL, NULL);
  if (ret < 0) {
    print_error("Unable to open input file", ret);
    result_string = (*env)->NewStringUTF(env, "appendMp4Segment failed: Could not open input");
    goto append_end;
  }
  ret = avformat_find_stream_info(ifmt_ctx, NULL);
  if (ret < 0) {
    print_error("Unable to retrieve stream info", ret);
    result_string = (*env)->NewStringUTF(env, "appendMp4Segment failed: Could not find stream info");
    goto append_end;
  }

  // --- Write Header if first time ---
  if (!session->header_written) {
    // (Stream creation logic remains the same)
    int stream_mapping_size_hdr = ifmt_ctx->nb_streams;
    int *stream_mapping_hdr = (int *) av_malloc_array(stream_mapping_size_hdr, sizeof(int));
    if (!stream_mapping_hdr) {
      ret = AVERROR(ENOMEM);
      goto append_end;
    }
    memset(stream_mapping_hdr, 0, stream_mapping_size_hdr * sizeof(int));

    int out_stream_count = 0;
    for (int i = 0; i < ifmt_ctx->nb_streams; i++) {
      AVStream *in_stream = ifmt_ctx->streams[i];
      AVCodecParameters *in_codecpar = in_stream->codecpar;
      if (in_codecpar->codec_type != AVMEDIA_TYPE_VIDEO && in_codecpar->codec_type != AVMEDIA_TYPE_AUDIO &&
          in_codecpar->codec_type != AVMEDIA_TYPE_SUBTITLE) {
        stream_mapping_hdr[i] = -1;
        continue;
      }
      stream_mapping_hdr[i] = out_stream_count++;
      AVStream *out_stream = avformat_new_stream(session->ofmt_ctx, NULL);
      if (!out_stream) {
        ret = AVERROR_UNKNOWN;
        av_freep(&stream_mapping_hdr);
        goto append_end;
      }
      ret = avcodec_parameters_copy(out_stream->codecpar, in_codecpar);
      if (ret < 0) {
        print_error("Failed to copy codec parameters", ret);
        av_freep(&stream_mapping_hdr);
        goto append_end;
      }
      out_stream->codecpar->codec_tag = 0;
      out_stream->time_base = in_stream->time_base; // Use input timebase initially for output stream
    }
    av_freep(&stream_mapping_hdr);

    // Ensure tracking array is allocated based on the *output* streams
    ret = ensure_stream_tracking(session, session->ofmt_ctx->nb_streams);
    if (ret < 0) {
      result_string = (*env)->NewStringUTF(env, "appendMp4Segment failed: Could not allocate DTS tracking");
      goto append_end;
    }

    // (HLS options setting remains the same)
    AVDictionary *opts = NULL;
    char seg_time_str[16];
    snprintf(seg_time_str, sizeof(seg_time_str), "%d", session->segment_duration);
    av_dict_set(&opts, "hls_time", seg_time_str, 0);
    char start_num_str[16];
    snprintf(start_num_str, sizeof(start_num_str), "%d", session->start_number);
    av_dict_set(&opts, "start_number", start_num_str, 0);
    av_dict_set(&opts, "hls_segment_filename", session->ts_pattern, 0);
    av_dict_set(&opts, "hls_flags", "append_list", 0);
    av_dict_set(&opts, "hls_list_size", "0", 0);
    av_dict_set(&opts, "hls_playlist_type", "event", 0);

    ret = avformat_write_header(session->ofmt_ctx, &opts);
    av_dict_free(&opts);
    if (ret < 0) {
      print_error("Failed to write header", ret);
      result_string = (*env)->NewStringUTF(env, "appendMp4Segment failed: Could not write header");
      goto append_end;
    }
    session->header_written = 1;
    fprintf(stderr, "HLS header written successfully.\n");
    dts_offset = 0; // First segment starts at 0
  } else {
    // Ensure tracking array is sufficient (should already be if header was written)
    ret = ensure_stream_tracking(session, session->ofmt_ctx->nb_streams);
    if (ret < 0) {
      result_string = (*env)->NewStringUTF(env, "appendMp4Segment failed: Could not ensure DTS tracking");
      goto append_end;
    }
    // Calculate the offset needed for this segment based on the last DTS written
    dts_offset = get_next_start_dts(session);
    // fprintf(stderr, "Calculated next start DTS offset: %lld\n", dts_offset);
  }

  // --- Rebuild stream mapping ---
  // (Mapping logic remains the same)
  int stream_mapping_size = ifmt_ctx->nb_streams;
  stream_mapping = (int *) av_malloc_array(stream_mapping_size, sizeof(int));
  if (!stream_mapping) {
    ret = AVERROR(ENOMEM);
    goto append_end;
  }
  memset(stream_mapping, 0, stream_mapping_size * sizeof(int));
  int out_stream_idx_counter = 0;
  for (int i = 0; i < ifmt_ctx->nb_streams; i++) {
    AVStream *in_stream = ifmt_ctx->streams[i];
    if (in_stream->codecpar->codec_type != AVMEDIA_TYPE_VIDEO &&
        in_stream->codecpar->codec_type != AVMEDIA_TYPE_AUDIO &&
        in_stream->codecpar->codec_type != AVMEDIA_TYPE_SUBTITLE) {
      stream_mapping[i] = -1;
    } else {
      if (out_stream_idx_counter < session->ofmt_ctx->nb_streams) { stream_mapping[i] = out_stream_idx_counter++; }
      else {
        fprintf(stderr, "Warning: Input stream %d has no corresponding output stream.\n", i);
        stream_mapping[i] = -1;
      }
    }
  }

  // --- Read packets, adjust timestamps, write ---
  av_init_packet(&pkt);
  while (av_read_frame(ifmt_ctx, &pkt) >= 0) {
    if (pkt.stream_index >= stream_mapping_size || stream_mapping[pkt.stream_index] < 0) {
      av_packet_unref(&pkt);
      continue;
    }

    int out_stream_index = stream_mapping[pkt.stream_index];
    AVStream *in_stream = ifmt_ctx->streams[pkt.stream_index];
    AVStream *out_stream = session->ofmt_ctx->streams[out_stream_index];

    // Rescale PTS/DTS/Duration first
    pkt.pts = av_rescale_q_rnd(pkt.pts, in_stream->time_base, out_stream->time_base,
                               AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
    pkt.dts = av_rescale_q_rnd(pkt.dts, in_stream->time_base, out_stream->time_base,
                               AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
    pkt.duration = av_rescale_q(pkt.duration, in_stream->time_base, out_stream->time_base);
    pkt.pos = -1;
    pkt.stream_index = out_stream_index; // Set output stream index *before* offset calculation

    // --- Calculate and Apply Offset ---
    // Find the DTS of the first packet in this input segment (after rescaling)
    if (first_packet_dts_in_segment_rescaled == AV_NOPTS_VALUE && pkt.dts != AV_NOPTS_VALUE) {
      first_packet_dts_in_segment_rescaled = pkt.dts;
      // fprintf(stderr, "First packet DTS in segment (rescaled): %lld\n", first_packet_dts_in_segment_rescaled);
    }

    // Calculate the actual offset to add to this packet's DTS/PTS
    // Offset = target_start_dts - first_packet_dts_in_segment_rescaled
    int64_t current_pkt_offset = 0;
    if (first_packet_dts_in_segment_rescaled != AV_NOPTS_VALUE) {
      // Ensure the first packet's DTS (after offset) is >= dts_offset
      current_pkt_offset = dts_offset - first_packet_dts_in_segment_rescaled;
      if (first_packet_dts_in_segment_rescaled + current_pkt_offset < dts_offset) {
        // This can happen if first DTS is slightly negative after rescaling
        current_pkt_offset = dts_offset - first_packet_dts_in_segment_rescaled;
      }
    } else {
      // If first packet had no DTS, just use the base offset (less accurate, might need adjustment later)
      current_pkt_offset = dts_offset;
    }

    if (pkt.pts != AV_NOPTS_VALUE) pkt.pts += current_pkt_offset;
    if (pkt.dts != AV_NOPTS_VALUE) pkt.dts += current_pkt_offset;
    // --- End Offset Calculation ---


    // --- Ensure Monotonic DTS and Valid PTS (CRITICAL STEP) --- (REVISED LOGIC)
    int64_t last_dts = (session->last_dts_written && out_stream_index < session->nb_streams_allocated)
                       ? session->last_dts_written[out_stream_index] : AV_NOPTS_VALUE; // Get last DTS or AV_NOPTS_VALUE

    // 1. Handle Packet DTS
    if (pkt.dts == AV_NOPTS_VALUE) {
      // If packet DTS is missing, assign one based on the last written DTS.
      // If it's the very first packet ever for this stream, start at 0.
      pkt.dts = (last_dts == AV_NOPTS_VALUE) ? 0 : (last_dts + 1);
      // fprintf(stderr, "Warning: Assigning DTS %lld to packet with no DTS (Stream %d, Append)\n", pkt.dts, out_stream_index);
    } else {
      // If packet DTS exists, ensure it's greater than the last written DTS.
      if (last_dts != AV_NOPTS_VALUE && pkt.dts <= last_dts) {
        // fprintf(stderr, "Adjusting DTS for stream %d (Append): %lld -> %lld\n",
        //         out_stream_index, pkt.dts, last_dts + 1);
        pkt.dts = last_dts + 1;
      }
        // If last_dts == AV_NOPTS_VALUE, this is the first packet with a DTS,
        // ensure it's non-negative (prefer 0 if it's negative).
      else if (last_dts == AV_NOPTS_VALUE && pkt.dts < 0) {
        // fprintf(stderr, "Adjusting initial negative DTS for stream %d (Append): %lld -> 0\n", out_stream_index, pkt.dts);
        pkt.dts = 0;
      }
    }

    // 2. Handle Packet PTS
    if (pkt.pts == AV_NOPTS_VALUE) {
      // If PTS is missing, set it equal to the (potentially adjusted) DTS.
      pkt.pts = pkt.dts;
    } else {
      // If PTS exists, ensure it's greater than or equal to the (potentially adjusted) DTS.
      if (pkt.pts < pkt.dts) {
        // fprintf(stderr, "Adjusting PTS for stream %d (Append): %lld -> %lld (to match DTS)\n",
        //         out_stream_index, pkt.pts, pkt.dts);
        pkt.pts = pkt.dts;
      }
    }
    // --- End DTS/PTS Check ---

    // Write the packet
    ret = av_interleaved_write_frame(session->ofmt_ctx, &pkt);

    // --- Update Tracking Info (only if write succeeded) ---
    if (ret >= 0) {
      // Store the *actual* DTS written (which might have been adjusted)
      if (session->last_dts_written && out_stream_index < session->nb_streams_allocated) {
        session->last_dts_written[out_stream_index] = pkt.dts;
      }
    }
    // --- End Update Tracking ---

    av_packet_unref(&pkt); // Unref packet regardless of write success

    if (ret < 0) {
      print_error("Failed to write packet", ret);
      result_string = (*env)->NewStringUTF(env, "appendMp4Segment failed: Could not write packet");
      goto append_end;
    }
  } // End while(av_read_frame)

  result_string = (*env)->NewStringUTF(env, "Segment appended successfully");

  append_end:
  av_freep(&stream_mapping);
  if (ifmt_ctx) avformat_close_input(&ifmt_ctx);
  if (inputFilePath) (*env)->ReleaseStringUTFChars(env, inputFilePathJ, inputFilePath);
  if (!result_string) { /* Set default error message if needed */
    if (ret < 0) {
      char err_msg[256];
      snprintf(err_msg, sizeof(err_msg), "appendMp4Segment failed with error: %s", av_err2str(ret));
      result_string = (*env)->NewStringUTF(env, err_msg);
    }
    else { result_string = (*env)->NewStringUTF(env, "appendMp4Segment finished with unknown state"); }
  }
  return result_string;
}


/*
 * Class:     com_litongjava_media_NativeMedia
 * Method:    insertSilentSegment
 * Signature: (JD)Ljava/lang/String;
 */
JNIEXPORT jstring JNICALL Java_com_litongjava_media_NativeMedia_insertSilentSegment
  (JNIEnv *env, jclass clazz, jlong sessionPtr, jdouble duration) {
  int ret = 0;
  AVCodecContext *codec_ctx = NULL;
  AVFrame *frame = NULL;
  AVPacket *pkt = NULL;
  AVPacket *pkt_flush = NULL;
  jstring result_string = NULL;
  HlsSession *session = NULL;
  int audio_stream_index = -1;
  AVStream *audio_stream = NULL;
  int64_t dts_offset = 0; // Offset for generated silence packets

  if (!sessionPtr) return (*env)->NewStringUTF(env, "Invalid session pointer");
  session = (HlsSession *) (uintptr_t) sessionPtr;
  if (!session->ofmt_ctx) return (*env)->NewStringUTF(env, "Invalid session: Output context is NULL");
  if (!session->header_written || session->ofmt_ctx->nb_streams == 0)
    return (*env)->NewStringUTF(env, "HLS header not written or no streams exist");

  // Find audio stream
  for (int i = 0; i < session->ofmt_ctx->nb_streams; i++) {
    if (session->ofmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
      audio_stream = session->ofmt_ctx->streams[i];
      audio_stream_index = i;
      break;
    }
  }
  if (!audio_stream) return (*env)->NewStringUTF(env, "No audio stream found in HLS session");
  // Ensure tracking is allocated before checking index validity
  ret = ensure_stream_tracking(session, session->ofmt_ctx->nb_streams);
  if (ret < 0) return (*env)->NewStringUTF(env, "Could not ensure DTS tracking for silence");
  if (audio_stream_index < 0 || audio_stream_index >= session->nb_streams_allocated)
    return (*env)->NewStringUTF(env, "Invalid audio stream index for DTS tracking");


  // --- Setup Audio Encoder ---
  // (Encoder setup logic remains largely the same)
  enum AVCodecID codec_id = audio_stream->codecpar->codec_id;
  if (codec_id == AV_CODEC_ID_NONE) codec_id = AV_CODEC_ID_AAC; // Prefer AAC for silence if none specified
  const AVCodec *codec = avcodec_find_encoder(codec_id);
  if (!codec) { codec = avcodec_find_encoder(AV_CODEC_ID_AAC); } // Fallback to AAC
  if (!codec) {
    char err_msg[100];
    snprintf(err_msg, sizeof(err_msg), "Encoder not found for %s or AAC", avcodec_get_name(codec_id));
    return (*env)->NewStringUTF(env, err_msg);
  }
  codec_ctx = avcodec_alloc_context3(codec);
  if (!codec_ctx) {
    ret = AVERROR(ENOMEM);
    print_error("Could not allocate codec context", ret);
    result_string = (*env)->NewStringUTF(env, "Could not allocate codec context");
    goto silent_end;
  }
  codec_ctx->sample_rate = audio_stream->codecpar->sample_rate;
  if (codec_ctx->sample_rate <= 0) codec_ctx->sample_rate = 44100;
  ret = av_channel_layout_copy(&codec_ctx->ch_layout, &audio_stream->codecpar->ch_layout);
  if (ret < 0 || codec_ctx->ch_layout.nb_channels <= 0) av_channel_layout_default(&codec_ctx->ch_layout, 2);
  codec_ctx->sample_fmt = AV_SAMPLE_FMT_NONE;
  if (codec->sample_fmts) {
    for (int i = 0; codec->sample_fmts[i] != AV_SAMPLE_FMT_NONE; i++) {
      if (codec->sample_fmts[i] == AV_SAMPLE_FMT_FLTP) {
        codec_ctx->sample_fmt = codec->sample_fmts[i];
        break;
      }
    }
    if (codec_ctx->sample_fmt == AV_SAMPLE_FMT_NONE) {
      for (int i = 0; codec->sample_fmts[i] != AV_SAMPLE_FMT_NONE; i++) {
        if (codec->sample_fmts[i] == AV_SAMPLE_FMT_S16) {
          codec_ctx->sample_fmt = codec->sample_fmts[i];
          break;
        }
      }
    }
    if (codec_ctx->sample_fmt == AV_SAMPLE_FMT_NONE) codec_ctx->sample_fmt = codec->sample_fmts[0];
  }
  else { codec_ctx->sample_fmt = AV_SAMPLE_FMT_FLTP; } // Default guess if no list
  if (codec_ctx->sample_fmt == AV_SAMPLE_FMT_NONE) {
    result_string = (*env)->NewStringUTF(env, "Could not determine supported sample format for encoder");
    goto silent_end;
  }
  codec_ctx->bit_rate = audio_stream->codecpar->bit_rate;
  if (codec_ctx->bit_rate <= 0) codec_ctx->bit_rate = 64000;
  codec_ctx->time_base = (AVRational) {1, codec_ctx->sample_rate};
  // Some encoders require frame_size to be set
  if (codec->capabilities & AV_CODEC_CAP_VARIABLE_FRAME_SIZE) {
    codec_ctx->frame_size = 1024; // Common value for AAC
  }

  ret = avcodec_open2(codec_ctx, codec, NULL);
  if (ret < 0) {
    print_error("Could not open encoder", ret);
    result_string = (*env)->NewStringUTF(env, "Could not open encoder");
    goto silent_end;
  }

  // --- Generate Silent Audio Frames ---
  int total_samples = (int) (duration * codec_ctx->sample_rate);
  if (total_samples <= 0) {
    result_string = (*env)->NewStringUTF(env, "Silent segment inserted successfully (0 duration)");
    goto silent_end;
  }
  int frame_size = codec_ctx->frame_size;
  if (frame_size <= 0) frame_size = 1024; // Use default if not set
  int remaining_samples = total_samples;
  AVRational sample_tb = (AVRational) {1, codec_ctx->sample_rate};

  // Calculate the DTS offset for the *first* packet of silence
  dts_offset = get_next_start_dts(session);
  // fprintf(stderr, "Calculated next start DTS offset for silence: %lld\n", dts_offset);
  int64_t current_pts_in_silence_codec_tb = 0; // Track relative PTS in *codec* timebase

  pkt = av_packet_alloc();
  if (!pkt) {
    ret = AVERROR(ENOMEM);
    goto silent_end_error;
  }

  while (remaining_samples > 0) {
    int current_samples = FFMIN(remaining_samples, frame_size);
    frame = av_frame_alloc();
    if (!frame) {
      ret = AVERROR(ENOMEM);
      goto silent_end_error;
    }
    frame->nb_samples = current_samples;
    frame->format = codec_ctx->sample_fmt;
    frame->sample_rate = codec_ctx->sample_rate;
    ret = av_channel_layout_copy(&frame->ch_layout, &codec_ctx->ch_layout);
    if (ret < 0) {
      print_error("Failed to copy channel layout to frame", ret);
      av_frame_free(&frame);
      goto silent_end_error;
    }
    ret = av_frame_get_buffer(frame, 0);
    if (ret < 0) {
      print_error("Could not allocate audio frame data", ret);
      av_frame_free(&frame);
      goto silent_end_error;
    }
    ret = av_samples_set_silence(frame->data, 0, frame->nb_samples, frame->ch_layout.nb_channels, frame->format);
    if (ret < 0) {
      print_error("Could not set silence on frame", ret);
      av_frame_free(&frame);
      goto silent_end_error;
    }

    // Set frame PTS relative to the start of the silent segment, in *codec* timebase
    frame->pts = current_pts_in_silence_codec_tb;

    ret = avcodec_send_frame(codec_ctx, frame);
    av_frame_free(&frame); // Free frame now
    if (ret < 0 && ret != AVERROR(EAGAIN) && ret != AVERROR_EOF) {
      print_error("Error sending silent frame to encoder", ret);
      goto silent_end_error;
    }

    // Receive packets until EAGAIN or EOF
    while (1) { // Loop to drain encoder after sending one frame
      int receive_ret = avcodec_receive_packet(codec_ctx, pkt);
      if (receive_ret == AVERROR(EAGAIN) || receive_ret == AVERROR_EOF) {
        // Need more input or EOF reached for this frame, break inner loop
        ret = (receive_ret == AVERROR_EOF) ? AVERROR_EOF : 0; // Keep ret 0 on EAGAIN
        break;
      } else if (receive_ret < 0) {
        print_error("Error receiving packet from encoder", receive_ret);
        ret = receive_ret; // Propagate error
        goto receive_loop_end; // Exit outer loop on critical error
      }

      // Rescale packet timestamps from codec TB to output stream TB
      av_packet_rescale_ts(pkt, codec_ctx->time_base, audio_stream->time_base);

      // Apply the calculated offset
      if (pkt->pts != AV_NOPTS_VALUE) pkt->pts += dts_offset;
      if (pkt->dts != AV_NOPTS_VALUE) pkt->dts += dts_offset;

      pkt->stream_index = audio_stream_index;

      // --- Ensure Monotonic DTS and Valid PTS (CRITICAL STEP) --- (REVISED LOGIC)
      int64_t last_dts = (session->last_dts_written && audio_stream_index < session->nb_streams_allocated)
                         ? session->last_dts_written[audio_stream_index] : AV_NOPTS_VALUE;

      // 1. Handle Packet DTS
      if (pkt->dts == AV_NOPTS_VALUE) {
        pkt->dts = (last_dts == AV_NOPTS_VALUE) ? 0 : (last_dts + 1);
        // fprintf(stderr, "Warning: Assigning DTS %lld to packet with no DTS (Stream %d, Silence)\n", pkt->dts, audio_stream_index);
      } else {
        if (last_dts != AV_NOPTS_VALUE && pkt->dts <= last_dts) {
          // fprintf(stderr, "Adjusting DTS for stream %d (Silence): %lld -> %lld\n",
          //         audio_stream_index, pkt->dts, last_dts + 1);
          pkt->dts = last_dts + 1;
        } else if (last_dts == AV_NOPTS_VALUE && pkt->dts < 0) {
          // fprintf(stderr, "Adjusting initial negative DTS for stream %d (Silence): %lld -> 0\n", audio_stream_index, pkt->dts);
          pkt->dts = 0;
        }
      }

      // 2. Handle Packet PTS
      if (pkt->pts == AV_NOPTS_VALUE) {
        pkt->pts = pkt->dts;
      } else {
        if (pkt->pts < pkt->dts) {
          // fprintf(stderr, "Adjusting PTS for stream %d (Silence): %lld -> %lld (to match DTS)\n",
          //         audio_stream_index, pkt->pts, pkt->dts);
          pkt->pts = pkt->dts;
        }
      }
      // --- End DTS/PTS Check ---

      // Write the packet
      int write_ret = av_interleaved_write_frame(session->ofmt_ctx, pkt);

      // --- Update Tracking Info (only if write succeeded) ---
      if (write_ret >= 0) {
        if (session->last_dts_written && audio_stream_index < session->nb_streams_allocated) {
          session->last_dts_written[audio_stream_index] = pkt->dts;
        }
      }
      // --- End Update Tracking ---

      av_packet_unref(pkt); // Unref packet inside receive loop

      if (write_ret < 0) {
        print_error("Failed to write silent packet", write_ret);
        ret = write_ret; // Propagate error
        goto receive_loop_end; // Exit outer loop on write error
      }
    } // end while receive loop
    receive_loop_end:;

    if (ret < 0 && ret != AVERROR_EOF) { // Check for critical errors from receive or write
      fprintf(stderr, "Error occurred during silent packet receive/write loop.\n");
      goto silent_end_error;
    }

    // Update relative PTS counter *within* the silent segment (in codec timebase)
    // Duration of 'current_samples' in codec timebase
    int64_t pts_increment_codec_tb = av_rescale_q(current_samples, sample_tb, codec_ctx->time_base);
    current_pts_in_silence_codec_tb += pts_increment_codec_tb;
    remaining_samples -= current_samples;

  } // end while(remaining_samples > 0)

  // --- Flush the encoder ---
  ret = avcodec_send_frame(codec_ctx, NULL); // Send flush signal
  if (ret < 0 && ret != AVERROR_EOF) {
    print_error("Error sending flush frame", ret);
    // Continue to try receiving, but report error later if nothing comes out
  }

  pkt_flush = av_packet_alloc();
  if (!pkt_flush) {
    ret = AVERROR(ENOMEM);
    goto silent_end_error;
  }
  while (1) { // Loop to drain the encoder completely
    int receive_ret = avcodec_receive_packet(codec_ctx, pkt_flush);
    if (receive_ret == AVERROR(EAGAIN)) {
      // Should not happen after sending NULL, but handle defensively
      fprintf(stderr, "Warning: Received EAGAIN after sending NULL flush frame.\n");
      continue; // Or break? Let's break.
      break;
    } else if (receive_ret == AVERROR_EOF) {
      ret = 0; // Successful flush
      break;
    } else if (receive_ret < 0) {
      print_error("Error receiving flushed packet", receive_ret);
      ret = receive_ret; // Store flush error
      break;
    }

    // Rescale packet timestamps
    av_packet_rescale_ts(pkt_flush, codec_ctx->time_base, audio_stream->time_base);

    // Apply offset
    if (pkt_flush->pts != AV_NOPTS_VALUE) pkt_flush->pts += dts_offset;
    if (pkt_flush->dts != AV_NOPTS_VALUE) pkt_flush->dts += dts_offset;

    pkt_flush->stream_index = audio_stream_index;

    // --- Ensure Monotonic DTS and Valid PTS (Flush) --- (REVISED LOGIC)
    int64_t last_dts = (session->last_dts_written && audio_stream_index < session->nb_streams_allocated)
                       ? session->last_dts_written[audio_stream_index] : AV_NOPTS_VALUE;

    // 1. Handle Packet DTS
    if (pkt_flush->dts == AV_NOPTS_VALUE) {
      pkt_flush->dts = (last_dts == AV_NOPTS_VALUE) ? 0 : (last_dts + 1);
      // fprintf(stderr, "Warning: Assigning DTS %lld to packet with no DTS (Stream %d, Flush)\n", pkt_flush->dts, audio_stream_index);
    } else {
      if (last_dts != AV_NOPTS_VALUE && pkt_flush->dts <= last_dts) {
        // fprintf(stderr, "Adjusting DTS for stream %d (Flush): %lld -> %lld\n",
        //         audio_stream_index, pkt_flush->dts, last_dts + 1);
        pkt_flush->dts = last_dts + 1;
      } else if (last_dts == AV_NOPTS_VALUE && pkt_flush->dts < 0) {
        // fprintf(stderr, "Adjusting initial negative DTS for stream %d (Flush): %lld -> 0\n", audio_stream_index, pkt_flush->dts);
        pkt_flush->dts = 0;
      }
    }

    // 2. Handle Packet PTS
    if (pkt_flush->pts == AV_NOPTS_VALUE) {
      pkt_flush->pts = pkt_flush->dts;
    } else {
      if (pkt_flush->pts < pkt_flush->dts) {
        // fprintf(stderr, "Adjusting PTS for stream %d (Flush): %lld -> %lld (to match DTS)\n",
        //         audio_stream_index, pkt_flush->pts, pkt_flush->dts);
        pkt_flush->pts = pkt_flush->dts;
      }
    }
    // --- End DTS/PTS Check ---

    // Write the packet
    int write_ret = av_interleaved_write_frame(session->ofmt_ctx, pkt_flush);

    // --- Update Tracking Info (Flush) ---
    if (write_ret >= 0) {
      if (session->last_dts_written && audio_stream_index < session->nb_streams_allocated) {
        session->last_dts_written[audio_stream_index] = pkt_flush->dts;
      }
    }
    // --- End Update Tracking ---

    av_packet_unref(pkt_flush); // Unref packet inside flush loop

    if (write_ret < 0) {
      print_error("Failed to write flushed silent packet", write_ret);
      ret = write_ret; // Store write error
      break; // Exit flush loop on write error
    }
  } // end while flush receive loop

  goto silent_end; // Go to cleanup

  silent_end_error: // Label for errors during generation/encoding
  if (ret >= 0) ret = AVERROR_UNKNOWN; // Ensure ret reflects an error state
  char err_msg[256];
  snprintf(err_msg, sizeof(err_msg), "insertSilentSegment failed with error: %s", av_err2str(ret));
  result_string = (*env)->NewStringUTF(env, err_msg);

  silent_end: // Common cleanup point
  av_frame_free(&frame); // Safe even if NULL
  av_packet_free(&pkt); // Safe even if NULL
  av_packet_free(&pkt_flush); // Safe even if NULL
  avcodec_free_context(&codec_ctx); // Safe even if NULL

  if (!result_string) { // Set final status message if no error occurred earlier
    if (ret < 0) { // Check if an error occurred during flush/write
      char flush_err_msg[256];
      snprintf(flush_err_msg, sizeof(flush_err_msg), "insertSilentSegment failed during flush/write with error: %s",
               av_err2str(ret));
      result_string = (*env)->NewStringUTF(env, flush_err_msg);
    } else {
      result_string = (*env)->NewStringUTF(env, "Silent segment inserted successfully");
    }
  }
  return result_string;
}


/*
 * Class:     com_litongjava_media_NativeMedia
 * Method:    finishPersistentHls
 * Signature: (JLjava/lang/String;)Ljava/lang/String;
 */
JNIEXPORT jstring JNICALL Java_com_litongjava_media_NativeMedia_finishPersistentHls
  (JNIEnv *env, jclass clazz, jlong sessionPtr, jstring playlistUrlJ) {
  int ret = 0;
  const char *playlistUrl = NULL; // Only used for releasing JNI string
  HlsSession *session = NULL;

  playlistUrl = (*env)->GetStringUTFChars(env, playlistUrlJ, NULL); // Get string for release later

  if (!sessionPtr) {
    if (playlistUrl) (*env)->ReleaseStringUTFChars(env, playlistUrlJ, playlistUrl);
    return (*env)->NewStringUTF(env, "Invalid session pointer");
  }
  session = (HlsSession *) (uintptr_t) sessionPtr;

  if (session->ofmt_ctx && session->header_written) {
    // Write the HLS endlist tag
    AVDictionary *opts = NULL;
    av_dict_set(&opts, "hls_playlist_type", "vod", 0); // Change type to VOD to signal end
    av_dict_set(&opts, "hls_flags", "append_list+omit_endlist",
                0); // Keep append_list? Maybe just omit_endlist is enough if trailer handles it. Let's try removing omit_endlist.
    // Let av_write_trailer handle the #EXT-X-ENDLIST tag implicitly

    ret = av_write_trailer(session->ofmt_ctx);
    if (ret < 0) print_error("Failed to write trailer", ret);
    else fprintf(stderr, "HLS trailer written successfully.\n");
    av_dict_free(&opts); // Free opts even though we didn't use them for write_trailer
  } else {
    fprintf(stderr, "Skipping trailer write (header not written or context invalid).\n");
  }

  // --- Cleanup ---
  if (session->ofmt_ctx) {
    if (!(session->ofmt_ctx->oformat->flags & AVFMT_NOFILE) && session->ofmt_ctx->pb) {
      avio_closep(&session->ofmt_ctx->pb); // Close the file IO context
    }
    avformat_free_context(session->ofmt_ctx); // Free the format context
  }
  free(session->last_dts_written); // Free the DTS tracking array
  free(session->ts_pattern);      // Free the duplicated pattern string
  free(session);                  // Free the session struct itself

  if (playlistUrl) (*env)->ReleaseStringUTFChars(env, playlistUrlJ, playlistUrl);

  if (ret < 0) return (*env)->NewStringUTF(env, "finishPersistentHls failed (trailer write error)");
  else return (*env)->NewStringUTF(env, "Persistent HLS session finished successfully");
}