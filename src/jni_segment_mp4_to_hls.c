#include "com_litongjava_media_NativeMedia.h"
#include <jni.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "native_media.h"

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
#include <time.h>

typedef struct HlsSessionNode {
  HlsSession *session;
  struct HlsSessionNode *next;
} HlsSessionNode;

// 全局链表头指针，用于维护所有活跃的 HLS 会话
static HlsSessionNode *g_hlsSessionHead = NULL;

// 从全局链表中删除一个会话
static void remove_hls_session(HlsSession *session) {
  HlsSessionNode **curr = &g_hlsSessionHead;
  while (*curr) {
    if ((*curr)->session == session) {
      HlsSessionNode *tmp = *curr;
      *curr = tmp->next;
      free(tmp);
      return;
    }
    curr = &((*curr)->next);
  }
}

// 辅助函数：查找全局列表中是否存在给定的会话
static HlsSession *find_hls_session(HlsSession *sessionPtrValue) {
  HlsSessionNode *curr = g_hlsSessionHead;
  while (curr) {
    if (curr->session == sessionPtrValue) {
      return curr->session;
    }
    curr = curr->next;
  }
  return NULL;
}


// 添加一个会话到全局链表
static void add_hls_session(HlsSession *session) {
  HlsSessionNode *node = (HlsSessionNode *) malloc(sizeof(HlsSessionNode));
  if (node) {
    node->session = session;
    node->next = g_hlsSessionHead;
    g_hlsSessionHead = node;
  }
}

JNIEXPORT jlong JNICALL Java_com_litongjava_media_NativeMedia_initPersistentHls
  (JNIEnv *env, jclass clazz, jstring playlistUrlJ, jstring tsPatternJ, jint startNumber, jint segDuration) {

  const char *playlistUrl = (*env)->GetStringUTFChars(env, playlistUrlJ, NULL);
  const char *tsPattern = (*env)->GetStringUTFChars(env, tsPatternJ, NULL);

  // --- 直接调用新写的 C 代码 ---
  HlsSession *session = init_hls_session(playlistUrl, tsPattern, startNumber, segDuration);

  (*env)->ReleaseStringUTFChars(env, playlistUrlJ, playlistUrl);
  (*env)->ReleaseStringUTFChars(env, tsPatternJ, tsPattern);

  if (!session) {
    return 0;
  }

  // 这里的 add_hls_session 是你原来代码中维护全局列表的逻辑，建议保留
  add_hls_session(session);

  return (jlong)(uintptr_t)session;
}

JNIEXPORT jstring JNICALL Java_com_litongjava_media_NativeMedia_appendVideoSegmentToHls
  (JNIEnv *env, jclass clazz, jlong sessionPtr, jstring inputFilePathJ) {

  HlsSession *session = (HlsSession *)(uintptr_t)sessionPtr;
  if (!session) {
    return (*env)->NewStringUTF(env, "Invalid HLS session pointer");
  }

  const char *inputFilePath = (*env)->GetStringUTFChars(env, inputFilePathJ, NULL);

  // --- 调用新写的 C 代码：它内部处理了 PTS 修复逻辑 ---
  char *error_msg = append_video_segment(session, inputFilePath);

  (*env)->ReleaseStringUTFChars(env, inputFilePathJ, inputFilePath);

  if (error_msg != NULL) {
    jstring result = (*env)->NewStringUTF(env, error_msg);
    free(error_msg); // 释放 C 内部 strdup 分配的内存
    return result;
  }

  // 返回成功信息
  char successMsg[128];
  snprintf(successMsg, sizeof(successMsg), "Success. Global offset: %lld", session->global_offset);
  return (*env)->NewStringUTF(env, successMsg);
}

JNIEXPORT jstring JNICALL Java_com_litongjava_media_NativeMedia_finishPersistentHls
  (JNIEnv *env, jclass clazz, jlong sessionPtr, jstring playlistUrlJ) {

  HlsSession *sessionInput = (HlsSession *)(uintptr_t)sessionPtr;
  HlsSession *session = find_hls_session(sessionInput); // 查找全局列表确保安全

  if (!session || session->closed) {
    return (*env)->NewStringUTF(env, "Session already freed or invalid");
  }

  const char *playlistUrl = (*env)->GetStringUTFChars(env, playlistUrlJ, NULL);

  // --- 调用新写的 C 代码 ---
  char *result_info = finish_hls_session(session, playlistUrl);

  // 从你的全局管理链表中移除
  remove_hls_session(session);

  (*env)->ReleaseStringUTFChars(env, playlistUrlJ, playlistUrl);

  jstring jResult = (*env)->NewStringUTF(env, result_info);
  free(result_info);
  return jResult;
}