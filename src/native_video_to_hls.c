#include "com_litongjava_media_NativeMedia.h"
#include <jni.h>
#include <stdlib.h>
#include "native_media.h"

// 声明纯 C 接口函数（如果 pure_mp4_to_hls.c 已经编译为库，可以通过头文件包含）
const char *split_mp4_to_hls(const char *playlistUrl, const char *inputMp4Path,
                             const char *tsPattern, int segmentDuration, int sceneIndex);

JNIEXPORT jstring JNICALL Java_com_litongjava_media_NativeMedia_splitVideoToHLS(
  JNIEnv *env, jclass clazz,
  jstring playlistUrlJ,
  jstring inputMp4PathJ,
  jstring tsPatternJ,
  jint segmentDuration) {

  // 从 Java 层字符串转换为 C 字符串
  const char *playlistUrl = (*env)->GetStringUTFChars(env, playlistUrlJ, NULL);
  const char *inputMp4Path = (*env)->GetStringUTFChars(env, inputMp4PathJ, NULL);
  const char *tsPattern = (*env)->GetStringUTFChars(env, tsPatternJ, NULL);

  // 调用纯 C 接口函数进行视频处理
  const char *result = split_video_to_hls(playlistUrl, inputMp4Path, tsPattern, segmentDuration);

  // 释放 JNI 字符串资源
  (*env)->ReleaseStringUTFChars(env, playlistUrlJ, playlistUrl);
  (*env)->ReleaseStringUTFChars(env, inputMp4PathJ, inputMp4Path);
  (*env)->ReleaseStringUTFChars(env, tsPatternJ, tsPattern);

  // 将结果转换为 jstring 返回给 Java
  return (*env)->NewStringUTF(env, result);
}
