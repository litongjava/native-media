#include "com_litongjava_media_NativeMedia.h"
#include "native_media.h"
#include <jni.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>

#include <libavformat/avformat.h>
#include <libavutil/timestamp.h>
#include <libswscale/swscale.h>
#include <libavcodec/avcodec.h>

// ---------- 工具：错误码转字符串 ----------
static void err2str(int errnum, char *buf, size_t buflen) {
  if (!buf || buflen == 0) return;
  av_strerror(errnum, buf, buflen);
}


// 图像写入函数 - 支持多种格式
static int write_image_frame(const char *filename, const AVFrame *rgb) {
  int ret = 0;
  FILE *f = NULL;
  const AVCodec *codec = NULL;
  AVCodecContext *enc_ctx = NULL;
  AVPacket *pkt = NULL;
  enum AVCodecID codec_id;

  // 根据文件扩展名选择编码器
  const char *ext = strrchr(filename, '.');
  if (!ext) ext = ".jpg"; // 默认JPEG

  if (strcasecmp(ext, ".png") == 0) {
    codec_id = AV_CODEC_ID_PNG;
  } else if (strcasecmp(ext, ".jpg") == 0 || strcasecmp(ext, ".jpeg") == 0) {
    codec_id = AV_CODEC_ID_MJPEG;
  } else if (strcasecmp(ext, ".bmp") == 0) {
    codec_id = AV_CODEC_ID_BMP;
  } else if (strcasecmp(ext, ".ppm") == 0) {
    codec_id = AV_CODEC_ID_PPM;
  } else {
    codec_id = AV_CODEC_ID_MJPEG; // 默认JPEG
  }

  // 尝试找到编码器
  codec = avcodec_find_encoder(codec_id);
  if (!codec && codec_id == AV_CODEC_ID_PNG) {
    // PNG失败，尝试JPEG
    printf("PNG encoder not found, trying JPEG...\n");
    codec_id = AV_CODEC_ID_MJPEG;
    codec = avcodec_find_encoder(codec_id);

    // 修改输出文件名
    char *new_filename = strdup(filename);
    char *dot = strrchr(new_filename, '.');
    if (dot) strcpy(dot, ".jpg");
    printf("Output file changed to: %s\n", new_filename);
  }

  if (!codec) {
    printf("No suitable image encoder found\n");
    return AVERROR_ENCODER_NOT_FOUND;
  }

  enc_ctx = avcodec_alloc_context3(codec);
  if (!enc_ctx) {
    ret = AVERROR(ENOMEM);
    goto end;
  }

  enc_ctx->width = rgb->width;
  enc_ctx->height = rgb->height;
  enc_ctx->pix_fmt = AV_PIX_FMT_RGB24;
  enc_ctx->time_base = (AVRational) {1, 25};

  // 根据编码器类型设置参数
  if (codec_id == AV_CODEC_ID_MJPEG) {
    enc_ctx->pix_fmt = AV_PIX_FMT_YUVJ420P; // JPEG通常使用YUV格式
    enc_ctx->qmin = 1;
    enc_ctx->qmax = 31;
    enc_ctx->global_quality = FF_QP2LAMBDA * 3; // 高质量
  } else if (codec_id == AV_CODEC_ID_PNG) {
    enc_ctx->compression_level = 6;
  }

  if ((ret = avcodec_open2(enc_ctx, codec, NULL)) < 0) {
    char errbuf[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(ret, errbuf, sizeof(errbuf));
    printf("Failed to open encoder %s: %s\n", codec->name, errbuf);
    goto end;
  }

  // 如果编码器需要YUV格式，进行转换
  AVFrame *enc_frame = rgb;
  struct SwsContext *sws_enc = NULL;

  if (enc_ctx->pix_fmt != AV_PIX_FMT_RGB24) {
    enc_frame = av_frame_alloc();
    if (!enc_frame) {
      ret = AVERROR(ENOMEM);
      goto end;
    }

    enc_frame->format = enc_ctx->pix_fmt;
    enc_frame->width = rgb->width;
    enc_frame->height = rgb->height;

    if ((ret = av_frame_get_buffer(enc_frame, 32)) < 0) goto end;

    sws_enc = sws_getContext(
      rgb->width, rgb->height, AV_PIX_FMT_RGB24,
      enc_frame->width, enc_frame->height, enc_ctx->pix_fmt,
      SWS_BILINEAR, NULL, NULL, NULL
    );

    if (!sws_enc) {
      ret = AVERROR(EINVAL);
      goto end;
    }

    sws_scale(sws_enc, (const uint8_t *const *) rgb->data, rgb->linesize,
              0, rgb->height, enc_frame->data, enc_frame->linesize);
  }

  pkt = av_packet_alloc();
  if (!pkt) {
    ret = AVERROR(ENOMEM);
    goto end;
  }

  if ((ret = avcodec_send_frame(enc_ctx, enc_frame)) < 0) goto end;
  ret = avcodec_receive_packet(enc_ctx, pkt);
  if (ret < 0) goto end;

  f = fopen(filename, "wb");
  if (!f) {
    ret = AVERROR(errno);
    goto end;
  }

  if (fwrite(pkt->data, 1, pkt->size, f) != (size_t) pkt->size) {
    ret = AVERROR(EIO);
    goto end;
  }

  end:
  if (sws_enc) sws_freeContext(sws_enc);
  if (enc_frame && enc_frame != rgb) av_frame_free(&enc_frame);
  if (f) fclose(f);
  if (pkt) av_packet_free(&pkt);
  if (enc_ctx) avcodec_free_context(&enc_ctx);
  return ret;
}

// ========== 纯 C 实现：可被 JNI/main 调用 ==========
int save_last_frame_c(const char *inputPath, const char *outputPath) {
  int ret = 0;
  AVFormatContext *fmt = NULL;
  int vindex = -1;
  AVCodecParameters *vpar = NULL;
  const AVCodec *vdec = NULL;
  AVCodecContext *vctx = NULL;
  AVFrame *frame = NULL, *last = NULL, *rgb = NULL;
  AVPacket *pkt = NULL;
  struct SwsContext *sws = NULL;

  if (!inputPath || !outputPath) return AVERROR(EINVAL);

  // 打开输入
  if ((ret = avformat_open_input(&fmt, inputPath, NULL, NULL)) < 0) goto cleanup;
  if ((ret = avformat_find_stream_info(fmt, NULL)) < 0) goto cleanup;

  vindex = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
  if (vindex < 0) {
    ret = vindex;
    goto cleanup;
  }

  vpar = fmt->streams[vindex]->codecpar;
  vdec = avcodec_find_decoder(vpar->codec_id);
  if (!vdec) {
    ret = AVERROR_DECODER_NOT_FOUND;
    goto cleanup;
  }

  vctx = avcodec_alloc_context3(vdec);
  if (!vctx) {
    ret = AVERROR(ENOMEM);
    goto cleanup;
  }
  if ((ret = avcodec_parameters_to_context(vctx, vpar)) < 0) goto cleanup;
  if ((ret = avcodec_open2(vctx, vdec, NULL)) < 0) goto cleanup;

  pkt = av_packet_alloc();
  frame = av_frame_alloc();
  last = av_frame_alloc();
  if (!pkt || !frame || !last) {
    ret = AVERROR(ENOMEM);
    goto cleanup;
  }

  // seek 到接近末尾
  int64_t target_ts = AV_NOPTS_VALUE;
  AVStream *vs = fmt->streams[vindex];
  if (vs->duration != AV_NOPTS_VALUE && vs->duration > 0) {
    int64_t back = av_rescale_q(2LL * AV_TIME_BASE, AV_TIME_BASE_Q, vs->time_base);
    target_ts = vs->duration - (back > 0 ? back : 1);
    if (target_ts < 0) target_ts = 0;                  // NEW: 边界保护
  } else if (fmt->duration != AV_NOPTS_VALUE && fmt->duration > 0) {
    int64_t back_us = 2LL * AV_TIME_BASE;
    int64_t near_end_us = (fmt->duration > back_us) ? (fmt->duration - back_us) : (fmt->duration - 1);
    if (near_end_us < 0) near_end_us = 0;              // NEW
    target_ts = av_rescale_q(near_end_us, AV_TIME_BASE_Q, vs->time_base);
  }
  if (target_ts != AV_NOPTS_VALUE) {
    av_seek_frame(fmt, vindex, target_ts, AVSEEK_FLAG_BACKWARD);
    avcodec_flush_buffers(vctx);
  }

  // 解码并保留“最后一帧”
  while ((ret = av_read_frame(fmt, pkt)) >= 0) {
    if (pkt->stream_index == vindex) {
      if ((ret = avcodec_send_packet(vctx, pkt)) == 0) {
        while ((ret = avcodec_receive_frame(vctx, frame)) == 0) {
          av_frame_unref(last);
          if ((ret = av_frame_ref(last, frame)) < 0) {
            av_frame_unref(frame);
            break;
          }
        }
        if (ret == AVERROR(EAGAIN)) ret = 0;    // 继续
        if (ret == AVERROR_EOF) ret = 0;    // NEW: 不把 EOF 作为失败
      } else if (ret == AVERROR(EAGAIN)) {
        ret = 0; // 继续
      } else {
        break;
      }
    }
    av_packet_unref(pkt);
  }

  // NEW: 如果读循环以 EOF 结束，把 ret 归零，允许后续冲刷
  if (ret == AVERROR_EOF) ret = 0;

  // NEW: 无条件冲刷解码器，取尾部残留帧
  {
    int dr = avcodec_send_packet(vctx, NULL);
    if (dr >= 0 || dr == AVERROR_EOF) {
      while ((dr = avcodec_receive_frame(vctx, frame)) == 0) {
        av_frame_unref(last);
        if ((ret = av_frame_ref(last, frame)) < 0) break;
      }
      if (dr == AVERROR_EOF || dr == AVERROR(EAGAIN)) {
        // 正常结束，忽略
      } else if (dr < 0) {
        ret = dr;
      }
    } else {
      ret = dr;
    }
  }
  if (ret < 0) goto cleanup;

  // NEW: 如果仍拿不到帧，缩短回退时间重试（1s→0s）
  if (!last || !last->data[0]) {
    const int back_sec_candidates[] = {1, 0};
    for (int i = 0; i < 2 && (!last || !last->data[0]); ++i) {
      int back_s = back_sec_candidates[i];

      int64_t retry_ts = AV_NOPTS_VALUE;
      AVStream *vs2 = fmt->streams[vindex];
      if (vs2->duration != AV_NOPTS_VALUE && vs2->duration > 0) {
        int64_t back = av_rescale_q((int64_t) back_s * AV_TIME_BASE, AV_TIME_BASE_Q, vs2->time_base);
        retry_ts = vs2->duration - (back > 0 ? back : 0);
        if (retry_ts < 0) retry_ts = 0;
      } else if (fmt->duration != AV_NOPTS_VALUE && fmt->duration > 0) {
        int64_t near_end_us = fmt->duration - (int64_t) back_s * AV_TIME_BASE;
        if (near_end_us < 0) near_end_us = 0;
        retry_ts = av_rescale_q(near_end_us, AV_TIME_BASE_Q, vs2->time_base);
      }
      if (retry_ts == AV_NOPTS_VALUE) continue;

      av_seek_frame(fmt, vindex, retry_ts, AVSEEK_FLAG_BACKWARD);
      avcodec_flush_buffers(vctx);

      // 快速再解码一点尾部
      while ((ret = av_read_frame(fmt, pkt)) >= 0) {
        if (pkt->stream_index == vindex) {
          if ((ret = avcodec_send_packet(vctx, pkt)) == 0) {
            while ((ret = avcodec_receive_frame(vctx, frame)) == 0) {
              av_frame_unref(last);
              if ((ret = av_frame_ref(last, frame)) < 0) {
                av_frame_unref(frame);
                break;
              }
            }
            if (ret == AVERROR(EAGAIN)) ret = 0;
            if (ret == AVERROR_EOF) ret = 0; // NEW
          } else if (ret == AVERROR(EAGAIN)) {
            ret = 0;
          } else {
            break;
          }
        }
        av_packet_unref(pkt);
      }
      if (ret == AVERROR_EOF) ret = 0;

      // 再冲刷一次
      avcodec_send_packet(vctx, NULL);
      while (avcodec_receive_frame(vctx, frame) == 0) {
        av_frame_unref(last);
        if ((ret = av_frame_ref(last, frame)) < 0) break;
      }
    }
  }

  if (ret < 0) goto cleanup;
  if (!last || !last->data[0]) {
    ret = AVERROR(EINVAL);
    goto cleanup;
  }

  // 转 RGB24
  rgb = av_frame_alloc();
  if (!rgb) {
    ret = AVERROR(ENOMEM);
    goto cleanup;
  }
  rgb->format = AV_PIX_FMT_RGB24;
  rgb->width = last->width;
  rgb->height = last->height;
  if ((ret = av_frame_get_buffer(rgb, 32)) < 0) goto cleanup;

  sws = sws_getContext(
    last->width, last->height, (enum AVPixelFormat) last->format,
    rgb->width, rgb->height, AV_PIX_FMT_RGB24,
    SWS_BILINEAR, NULL, NULL, NULL
  );
  if (!sws) {
    ret = AVERROR(EINVAL);
    goto cleanup;
  }

  if (sws_scale(sws, (const uint8_t *const *) last->data, last->linesize,
                0, last->height, rgb->data, rgb->linesize) <= 0) {
    ret = AVERROR_EXTERNAL;
    goto cleanup;
  }

  // 写 PNG
  ret = write_image_frame(outputPath, rgb);

  cleanup:
  if (sws) sws_freeContext(sws);
  if (rgb) av_frame_free(&rgb);
  if (last) av_frame_free(&last);
  if (frame) av_frame_free(&frame);
  if (pkt) av_packet_free(&pkt);
  if (vctx) avcodec_free_context(&vctx);
  if (fmt) avformat_close_input(&fmt);
  return ret;
}


// ========== JNI 包装层：仅做参数转换并调用 ==========
JNIEXPORT jint JNICALL Java_com_litongjava_media_NativeMedia_saveLastFrame
  (JNIEnv *env, jclass clazz, jstring inputJ, jstring outputJ) {
  if (!inputJ || !outputJ) return AVERROR(EINVAL);

  const char *inputPath = (*env)->GetStringUTFChars(env, inputJ, NULL);
  const char *outputPath = (*env)->GetStringUTFChars(env, outputJ, NULL);

  int rc = save_last_frame_c(inputPath, outputPath);

  if (outputPath) (*env)->ReleaseStringUTFChars(env, outputJ, outputPath);
  if (inputPath) (*env)->ReleaseStringUTFChars(env, inputJ, inputPath);

  return (jint) rc;
}
