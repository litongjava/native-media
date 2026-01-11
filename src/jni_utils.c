#include <jni.h>
#include <stdlib.h>
#include <stdint.h>
#include "native_media.h"
#ifdef _WIN32

#include <stringapiset.h>

/**
 * 将 Java 的 jstring 转换为 UTF-8 编码的 C 字符串（支持中文路径）
 */
char *jstringToChar(JNIEnv *env, jstring jStr) {
  const jchar *raw = (*env)->GetStringChars(env, jStr, NULL);
  jsize len = (*env)->GetStringLength(env, jStr);
  int size_needed = WideCharToMultiByte(CP_UTF8, 0, (LPCWCH) raw, len, NULL, 0, NULL, NULL);
  char *strTo = (char *) malloc(size_needed + 1);
  WideCharToMultiByte(CP_UTF8, 0, (LPCWCH) raw, len, strTo, size_needed, NULL, NULL);
  strTo[size_needed] = '\0';
  (*env)->ReleaseStringChars(env, jStr, raw);
  return strTo;
}

#else
#include <string.h>
/**
 * 非 Windows 平台直接返回 UTF-8 编码的字符串
 */
char* jstringToChar(JNIEnv* env, jstring jStr) {
    const char* chars = (*env)->GetStringUTFChars(env, jStr, NULL);
    char* copy = strdup(chars);
    (*env)->ReleaseStringUTFChars(env, jStr, chars);
    return copy;
}
#endif