#include "com_litongjava_media_NativeMedia.h"
#include <jni.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32

#include <windows.h>

#endif

// FFmpeg 相关头文件（如果需要真实实现 HLS 分段请使用 FFmpeg API）
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
#include <libavutil/audio_fifo.h>

/**
 * 模拟使用 FFmpeg/ native‑media 库将 MP4 转换为 HLS 分段
 * 实际实现时，请调用对应的 native‑media 接口进行处理
 *
 * 此处仅模拟生成若干 TS 分段文件名，真实逻辑中应分段并写 TS 文件
 */
static int simulate_split_mp4_to_hls(const char *inputMp4Path,
                                     const char *tsPattern,
                                     int sceneIndex,
                                     int segmentDuration,
                                     char ***outSegments,
                                     int *outCount) {
  // 模拟分段数量
  int segCount = 3;
  char **segments = (char **) malloc(sizeof(char *) * segCount);
  if (!segments) {
    return -1;
  }
  for (int i = 0; i < segCount; i++) {
    segments[i] = (char *) malloc(256);
    if (!segments[i]) {
      // 释放已分配内存
      for (int j = 0; j < i; j++) {
        free(segments[j]);
      }
      free(segments);
      return -1;
    }
    // 根据 tsPattern 模板生成 TS 文件名，sceneIndex 为起始编号
    // 例如 tsPattern 为 "C:/videos/segment_%d.ts"
    sprintf(segments[i], tsPattern, sceneIndex + i);
    // 此处应调用 native‑media/FFmpeg 进行真实分段，并写 TS 文件到磁盘
  }
  *outSegments = segments;
  *outCount = segCount;
  return 0;
}

JNIEXPORT jstring JNICALL
Java_com_litongjava_media_NativeMedia_splitMp4ToHLS(JNIEnv *env, jclass clazz,
                                                    jstring playlistUrlJ,
                                                    jstring inputMp4PathJ,
                                                    jstring tsPatternJ,
                                                    jint sceneIndex,
                                                    jint segmentDuration) {
  // 1. 获取 Java 传入的字符串参数
  const char *playlistUrl = (*env)->GetStringUTFChars(env, playlistUrlJ, NULL);
  const char *inputMp4Path = (*env)->GetStringUTFChars(env, inputMp4PathJ, NULL);
  const char *tsPattern = (*env)->GetStringUTFChars(env, tsPatternJ, NULL);

  // 2. 使用 native‑media/FFmpeg 接口将 MP4 分段转换为 HLS
  char **segmentFiles = NULL;
  int segmentCount = 0;
  if (simulate_split_mp4_to_hls(inputMp4Path, tsPattern, sceneIndex, segmentDuration,
                                &segmentFiles, &segmentCount) != 0) {
    (*env)->ReleaseStringUTFChars(env, playlistUrlJ, playlistUrl);
    (*env)->ReleaseStringUTFChars(env, inputMp4PathJ, inputMp4Path);
    (*env)->ReleaseStringUTFChars(env, tsPatternJ, tsPattern);
    return (*env)->NewStringUTF(env, "HLS 分段转换失败");
  }

  // 3. 根据 TS 分段信息构造 m3u8 片段
  // 每个分段格式：#EXTINF:<duration>,\n<ts_filename>\n
  char m3u8Snippet[4096] = {0};
  for (int i = 0; i < segmentCount; i++) {
    char line[512] = {0};
    snprintf(line, sizeof(line), "#EXTINF:%d,\n%s\n", segmentDuration, segmentFiles[i]);
    strcat(m3u8Snippet, line);
  }

  // 4. 打开播放列表文件，注意：如果文件中已经存在 "#EXT-X-ENDLIST"，则不能追加
  FILE *playlistFile = NULL;
#ifdef _WIN32
  // Windows 环境下需要转换 UTF-8 路径到宽字符
  int size_needed = MultiByteToWideChar(CP_UTF8, 0, playlistUrl, -1, NULL, 0);
  wchar_t *wplaylistUrl = (wchar_t *) malloc(size_needed * sizeof(wchar_t));
  if (wplaylistUrl == NULL) {
    // 内存分配失败，释放资源后返回
    goto cleanup_fail;
  }
  MultiByteToWideChar(CP_UTF8, 0, playlistUrl, -1, wplaylistUrl, size_needed);
  playlistFile = _wfopen(wplaylistUrl, L"r+");
  free(wplaylistUrl);
#else
  playlistFile = fopen(playlistUrl, "r+");
#endif

  if (playlistFile == NULL) {
    // 如果文件不存在则创建
#ifdef _WIN32
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, playlistUrl, -1, NULL, 0);
    wchar_t *wplaylistUrl = (wchar_t *) malloc(size_needed * sizeof(wchar_t));
    if (wplaylistUrl == NULL) {
      goto cleanup_fail;
    }
    MultiByteToWideChar(CP_UTF8, 0, playlistUrl, -1, wplaylistUrl, size_needed);
    playlistFile = _wfopen(wplaylistUrl, L"w+");
    free(wplaylistUrl);
#else
    playlistFile = fopen(playlistUrl, "w+");
#endif
    if (playlistFile == NULL) {
      goto cleanup_fail;
    }
  }

  // 检查播放列表内容中是否包含 "#EXT-X-ENDLIST"
  fseek(playlistFile, 0, SEEK_END);
  long fileSize = ftell(playlistFile);
  fseek(playlistFile, 0, SEEK_SET);
  char *fileContent = (char *) malloc(fileSize + 1);
  if (fileContent == NULL) {
    fclose(playlistFile);
    goto cleanup_fail;
  }
  fread(fileContent, 1, fileSize, playlistFile);
  fileContent[fileSize] = '\0';

  if (strstr(fileContent, "#EXT-X-ENDLIST") != NULL) {
    // 如果已结束则不能追加
    free(fileContent);
    fclose(playlistFile);
    // 释放 TS 分段文件名内存
    for (int i = 0; i < segmentCount; i++) {
      free(segmentFiles[i]);
    }
    free(segmentFiles);
    (*env)->ReleaseStringUTFChars(env, playlistUrlJ, playlistUrl);
    (*env)->ReleaseStringUTFChars(env, inputMp4PathJ, inputMp4Path);
    (*env)->ReleaseStringUTFChars(env, tsPatternJ, tsPattern);
    return (*env)->NewStringUTF(env, "播放列表已包含 #EXT-X-ENDLIST，不可追加");
  }
  free(fileContent);

  // 追加 m3u8 片段
  fseek(playlistFile, 0, SEEK_END);
  if (fwrite(m3u8Snippet, 1, strlen(m3u8Snippet), playlistFile) != strlen(m3u8Snippet)) {
    fclose(playlistFile);
    goto cleanup_fail;
  }
  fclose(playlistFile);

  // 释放 TS 分段生成时申请的内存
  for (int i = 0; i < segmentCount; i++) {
    free(segmentFiles[i]);
  }
  free(segmentFiles);

  (*env)->ReleaseStringUTFChars(env, playlistUrlJ, playlistUrl);
  (*env)->ReleaseStringUTFChars(env, inputMp4PathJ, inputMp4Path);
  (*env)->ReleaseStringUTFChars(env, tsPatternJ, tsPattern);

  return (*env)->NewStringUTF(env, "HLS 分段及播放列表更新成功");

  cleanup_fail:
  if (segmentFiles) {
    for (int i = 0; i < segmentCount; i++) {
      if (segmentFiles[i])
        free(segmentFiles[i]);
    }
    free(segmentFiles);
  }
  (*env)->ReleaseStringUTFChars(env, playlistUrlJ, playlistUrl);
  (*env)->ReleaseStringUTFChars(env, inputMp4PathJ, inputMp4Path);
  (*env)->ReleaseStringUTFChars(env, tsPatternJ, tsPattern);
  return (*env)->NewStringUTF(env, "HLS 分段处理失败");
}
