#include "com_litongjava_media_NativeMedia.h"
#include <jni.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/timestamp.h>
#include <libavutil/error.h>
#ifdef _WIN32
#include <stringapiset.h>
#endif

// 辅助函数：新建一个输出段
// 参数说明：
//   ofmt_ctx      —— 指向当前输出 AVFormatContext 的指针（若不为 NULL，则先关闭上个段）
//   ifmt_ctx      —— 输入文件上下文（用于复制流信息）
//   extension     —— 输出容器格式（例如 "mp3", "mp4", "mkv" 等）
//   base_name     —— 输入文件去除扩展名的基础名称
//   seg_index     —— 本次段的序号
//   seg_filename  —— 用于保存本次段文件名的缓冲区，其大小 seg_filename_size 指定
//   seg_filename_size —— 文件名缓冲区大小
//   seg_names     —— 指向存放段文件名的 char* 数组指针（动态扩容）
//   seg_count     —— 当前已保存的段数
//   seg_capacity  —— 数组的当前容量
// 返回 0 表示成功，否则返回负错误码
static int open_new_segment(AVFormatContext **ofmt_ctx, AVFormatContext *ifmt_ctx,
                            const char *extension, const char *base_name,
                            int seg_index, char *seg_filename, size_t seg_filename_size,
                            char ***seg_names, int *seg_count, int *seg_capacity) {
  int ret;

  // 如果已有一个输出上下文，则先结束上一个段
  if (*ofmt_ctx) {
    ret = av_write_trailer(*ofmt_ctx);
    if (ret < 0)
      return ret;
    if (!((*ofmt_ctx)->oformat->flags & AVFMT_NOFILE) && (*ofmt_ctx)->pb)
      avio_closep(&(*ofmt_ctx)->pb);

    // 保存上个段文件名到数组中
    char *name_copy = strdup(seg_filename);
    if (!name_copy)
      return AVERROR(ENOMEM);
    if (*seg_count >= *seg_capacity) {
      *seg_capacity *= 2;
      char **tmp = realloc(*seg_names, (*seg_capacity) * sizeof(char *));
      if (!tmp)
        return AVERROR(ENOMEM);
      *seg_names = tmp;
    }
    (*seg_names)[*seg_count] = name_copy;
    (*seg_count)++;
    avformat_free_context(*ofmt_ctx);
    *ofmt_ctx = NULL;
  }

  // 构造新段文件名：例如 "input_segment_0.mp3"
  snprintf(seg_filename, seg_filename_size, "%s_segment_%d.%s", base_name, seg_index, extension);

  // 分配输出格式上下文
  ret = avformat_alloc_output_context2(ofmt_ctx, NULL, extension, seg_filename);
  if (ret < 0 || !(*ofmt_ctx))
    return ret;

  // 对输入文件中的每个流都创建一个输出流（流拷贝）
  for (unsigned int i = 0; i < ifmt_ctx->nb_streams; i++) {
    AVStream *in_stream = ifmt_ctx->streams[i];
    AVStream *out_stream = avformat_new_stream(*ofmt_ctx, NULL);
    if (!out_stream)
      return AVERROR_UNKNOWN;
    ret = avcodec_parameters_copy(out_stream->codecpar, in_stream->codecpar);
    if (ret < 0)
      return ret;
    out_stream->codecpar->codec_tag = 0;
    out_stream->time_base = in_stream->time_base;
  }

  // 打开输出文件（若格式需要文件操作）
  if (!((*ofmt_ctx)->oformat->flags & AVFMT_NOFILE)) {
    ret = avio_open(&(*ofmt_ctx)->pb, seg_filename, AVIO_FLAG_WRITE);
    if (ret < 0)
      return ret;
  }

  // 写文件头
  ret = avformat_write_header(*ofmt_ctx, NULL);
  return ret;
}



JNIEXPORT jobjectArray JNICALL
Java_com_litongjava_media_NativeMedia_split(JNIEnv *env, jclass clazz, jstring inputPath, jlong segSize) {
  char input_file[1024] = {0};
#ifdef _WIN32
  // 获取 jstring 的宽字符表示
  const jchar *inputChars = (*env)->GetStringChars(env, inputPath, NULL);
  if (!inputChars) {
    return NULL;
  }
  jsize inputLen = (*env)->GetStringLength(env, inputPath);

  // 构造宽字符缓冲区
  wchar_t wInput[1024] = {0};
  if (inputLen >= 1024) inputLen = 1023;
  for (int i = 0; i < inputLen; i++) {
    wInput[i] = (wchar_t) inputChars[i];
  }
  wInput[inputLen] = L'\0';
  (*env)->ReleaseStringChars(env, inputPath, inputChars);

  // 将宽字符转换为 UTF-8 字符串
  int utf8Len = WideCharToMultiByte(CP_UTF8, 0, wInput, -1, NULL, 0, NULL, NULL);
  if (utf8Len <= 0 || utf8Len > 1024) {
    return NULL;
  }
  WideCharToMultiByte(CP_UTF8, 0, wInput, -1, input_file, sizeof(input_file), NULL, NULL);
#else
  // 非 Windows 平台直接获取 UTF-8 字符串
    const char *tmp = (*env)->GetStringUTFChars(env, inputPath, NULL);
    if (!tmp) return NULL;
    strncpy(input_file, tmp, sizeof(input_file) - 1);
    (*env)->ReleaseStringUTFChars(env, inputPath, tmp);
#endif

  // 以下保持原有逻辑不变
  int ret = 0;
  AVFormatContext *ifmt_ctx = NULL;
  ret = avformat_open_input(&ifmt_ctx, input_file, NULL, NULL);
  if (ret < 0) {
    return NULL;
  }
  ret = avformat_find_stream_info(ifmt_ctx, NULL);
  if (ret < 0) {
    avformat_close_input(&ifmt_ctx);
    return NULL;
  }

  // 根据输入文件名获取基础名和扩展名
  const char *dot = strrchr(input_file, '.');
  char extension[32] = {0};
  if (dot && strlen(dot) > 1) {
    strncpy(extension, dot + 1, sizeof(extension) - 1);
  } else {
    strcpy(extension, "mp4"); // 默认容器
  }
  size_t base_len = dot ? (dot - input_file) : strlen(input_file);
  char base_name[1024] = {0};
  strncpy(base_name, input_file, base_len);
  base_name[base_len] = '\0';

  // 准备存储段文件名的动态数组
  int seg_capacity = 10;
  int seg_count = 0;
  char **seg_names = (char **) malloc(seg_capacity * sizeof(char *));
  if (!seg_names) {
    avformat_close_input(&ifmt_ctx);
    return NULL;
  }

  int seg_index = 0;
  AVFormatContext *ofmt_ctx = NULL;
  char seg_filename[1024] = {0};

  // 初始化第一个段
  ret = open_new_segment(&ofmt_ctx, ifmt_ctx, extension, base_name, seg_index, seg_filename,
                         sizeof(seg_filename), &seg_names, &seg_count, &seg_capacity);
  if (ret < 0) {
    free(seg_names);
    avformat_close_input(&ifmt_ctx);
    return NULL;
  }
  seg_index++;

  AVPacket *pkt = av_packet_alloc();
  if (!pkt) {
    if (ofmt_ctx) {
      av_write_trailer(ofmt_ctx);
      if (!(ofmt_ctx->oformat->flags & AVFMT_NOFILE) && ofmt_ctx->pb)
        avio_closep(&ofmt_ctx->pb);
      avformat_free_context(ofmt_ctx);
    }
    free(seg_names);
    avformat_close_input(&ifmt_ctx);
    return NULL;
  }

  // 读取输入数据包，并写入当前段（流拷贝）
  while (av_read_frame(ifmt_ctx, pkt) >= 0) {
    AVStream *in_stream = ifmt_ctx->streams[pkt->stream_index];
    AVStream *out_stream = ofmt_ctx->streams[pkt->stream_index];
    pkt->pts = av_rescale_q_rnd(pkt->pts, in_stream->time_base, out_stream->time_base,
                                AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
    pkt->dts = av_rescale_q_rnd(pkt->dts, in_stream->time_base, out_stream->time_base,
                                AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
    pkt->duration = av_rescale_q(pkt->duration, in_stream->time_base, out_stream->time_base);
    pkt->pos = -1;

    ret = av_interleaved_write_frame(ofmt_ctx, pkt);
    if (ret < 0)
      break;

    if (ofmt_ctx->pb && ofmt_ctx->pb->pos >= segSize) {
      ret = open_new_segment(&ofmt_ctx, ifmt_ctx, extension, base_name, seg_index, seg_filename,
                             sizeof(seg_filename), &seg_names, &seg_count, &seg_capacity);
      if (ret < 0)
        break;
      seg_index++;
    }
    av_packet_unref(pkt);
  }

  if (ofmt_ctx) {
    av_write_trailer(ofmt_ctx);
    if (!(ofmt_ctx->oformat->flags & AVFMT_NOFILE) && ofmt_ctx->pb)
      avio_closep(&ofmt_ctx->pb);
    char *name_copy = strdup(seg_filename);
    if (name_copy) {
      if (seg_count >= seg_capacity) {
        seg_capacity *= 2;
        seg_names = realloc(seg_names, seg_capacity * sizeof(char *));
      }
      seg_names[seg_count++] = name_copy;
    }
    avformat_free_context(ofmt_ctx);
    ofmt_ctx = NULL;
  }

  av_packet_free(&pkt);
  avformat_close_input(&ifmt_ctx);

  // 构造 Java 字符串数组返回结果
  jclass strClass = (*env)->FindClass(env, "java/lang/String");
  jobjectArray jresult = (*env)->NewObjectArray(env, seg_count, strClass, NULL);
  for (int i = 0; i < seg_count; i++) {
    jstring jstr = (*env)->NewStringUTF(env, seg_names[i]);
    (*env)->SetObjectArrayElement(env, jresult, i, jstr);
    free(seg_names[i]);
  }
  free(seg_names);
  return jresult;
}