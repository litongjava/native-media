#include "com_litongjava_media_NativeMedia.h"
#include "native_mp3.h"
#include <jni.h>
#include <stdlib.h>
#include <string.h>

#include <libavutil/samplefmt.h>


#ifdef _WIN32

#include <stringapiset.h>

#endif

jstring JNICALL Java_com_litongjava_media_NativeMedia_toMp3(JNIEnv *env, jclass clazz, jstring inputPath) {
  // 从 Java 获取输入文件路径（UTF-8 编码）
  const char *input_file = (*env)->GetStringUTFChars(env, inputPath, NULL);
  if (!input_file) {
    return (*env)->NewStringUTF(env, "Error: Failed to get input file path");
  }

  // 构造输出文件名：如果输入文件名有扩展名则替换为 .mp3，否则追加 .mp3
  char *output_file = NULL;
  size_t input_len = strlen(input_file);
  const char *dot = strrchr(input_file, '.');
  if (dot != NULL) {
    size_t base_len = dot - input_file;
    output_file = (char *) malloc(base_len + 4 + 1); // base + ".mp3" + '\0'
    if (!output_file) {
      (*env)->ReleaseStringUTFChars(env, inputPath, input_file);
      return (*env)->NewStringUTF(env, "Error: Memory allocation failed");
    }
    strncpy(output_file, input_file, base_len);
    output_file[base_len] = '\0';
    strcat(output_file, ".mp3");
  } else {
    output_file = (char *) malloc(input_len + 4 + 1);
    if (!output_file) {
      (*env)->ReleaseStringUTFChars(env, inputPath, input_file);
      return (*env)->NewStringUTF(env, "Error: Memory allocation failed");
    }
    strcpy(output_file, input_file);
    strcat(output_file, ".mp3");
  }
  jstring result;
  char *error_buffer = convert_to_mp3(input_file, output_file);

  result = (*env)->NewStringUTF(env, error_buffer);
  (*env)->ReleaseStringUTFChars(env, inputPath, input_file);
  if (output_file) free(output_file);
  return result;
}

JNIEXPORT jstring JNICALL Java_com_litongjava_media_NativeMedia_toMp3ForSilence(JNIEnv *env, jclass, jstring inputPath,
                                                                                jdouble insertion_silence_duration) {

  // 从 Java 获取输入文件路径（UTF-8 编码）
  const char *input_file = (*env)->GetStringUTFChars(env, inputPath, NULL);
  if (!input_file) {
    return (*env)->NewStringUTF(env, "Error: Failed to get input file path");
  }
  double silence_duration = (double) insertion_silence_duration;

  // 构造输出文件名：如果输入文件名有扩展名则替换为 .mp3，否则追加 .mp3
  char *output_file = NULL;
  size_t input_len = strlen(input_file);
  const char *dot = strrchr(input_file, '.');
  if (dot != NULL) {
    size_t base_len = dot - input_file;
    output_file = (char *) malloc(base_len + 4 + 1); // base + ".mp3" + '\0'
    if (!output_file) {
      (*env)->ReleaseStringUTFChars(env, inputPath, input_file);
      return (*env)->NewStringUTF(env, "Error: Memory allocation failed");
    }
    strncpy(output_file, input_file, base_len);
    output_file[base_len] = '\0';
    strcat(output_file, ".mp3");
  } else {
    output_file = (char *) malloc(input_len + 4 + 1);
    if (!output_file) {
      (*env)->ReleaseStringUTFChars(env, inputPath, input_file);
      return (*env)->NewStringUTF(env, "Error: Memory allocation failed");
    }
    strcpy(output_file, input_file);
    strcat(output_file, ".mp3");
  }
  jstring result;
  char *error_buffer = convert_to_mp3_for_silence(input_file, output_file, silence_duration);

  result = (*env)->NewStringUTF(env, error_buffer);
  (*env)->ReleaseStringUTFChars(env, inputPath, input_file);
  if (output_file) free(output_file);
  return result;

}
