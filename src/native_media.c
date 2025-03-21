#include "com_litongjava_media_NativeMedia.h"
#include <jni.h>
#include <stdio.h>
// FFmpeg header
#include <libavformat/avformat.h>

JNIEXPORT jobjectArray JNICALL Java_com_litongjava_media_NativeMedia_supportFormats(JNIEnv *env, jclass clazz) {
  const AVInputFormat *ifmt = NULL;
  void *iter = NULL;
  int count = 0;

  // First pass: count supported container formats
  while ((ifmt = av_demuxer_iterate(&iter))) {
    count++;
  }

  // Create a new Java String array (java.lang.String[]) with the count
  jclass stringClass = (*env)->FindClass(env, "java/lang/String");
  jobjectArray result = (*env)->NewObjectArray(env, count, stringClass, NULL);

  // Reset iterator for second pass
  iter = NULL;
  int index = 0;
  while ((ifmt = av_demuxer_iterate(&iter))) {
    const char *name = ifmt->name;
    const char *description = ifmt->long_name ? ifmt->long_name : "No description";

    // Construct a string in the format "name: description"
    char buffer[512];
    snprintf(buffer, sizeof(buffer), "%s: %s", name, description);

    // Convert C string to Java String and set the element in the array
    jstring jstr = (*env)->NewStringUTF(env, buffer);
    (*env)->SetObjectArrayElement(env, result, index, jstr);
    index++;
  }

  return result;
}
