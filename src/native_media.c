#include "com_litongjava_media_NativeMedia.h"
#include <jni.h>
#include <stdio.h>

JNIEXPORT jobjectArray JNICALL
Java_com_litongjava_media_NativeMedia_splitMp3(JNIEnv *env, jclass clazz, jstring srcPath, jlong size) {
  printf("Hello JNI!\n");

  jclass stringClass = (*env)->FindClass(env, "java/lang/String");
  if (stringClass == NULL) {
    return NULL;
  }
  jobjectArray result = (*env)->NewObjectArray(env, 0, stringClass, NULL);
  return result;
}