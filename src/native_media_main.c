#include <jni.h>
#include <stdio.h>
#include "com_litongjava_media_NativeMedia.h"

int main() {
  JavaVM *jvm;
  JNIEnv *env;
  JavaVMInitArgs vm_args;
  JavaVMOption options[1];

  // 设置 JVM 参数，例如 classpath
  options[0].optionString = "-Djava.class.path=.";
  vm_args.version = JNI_VERSION_1_8;
  vm_args.nOptions = 1;
  vm_args.options = options;
  vm_args.ignoreUnrecognized = JNI_FALSE;

  // 创建 JVM
  jint res = JNI_CreateJavaVM(&jvm, (void **)&env, &vm_args);
  if (res != JNI_OK) {
    fprintf(stderr, "无法创建 JVM\n");
    return -1;
  }

  // 这里可以调用 JNI 方法，但需要确保参数正确
  jclass clazz = NULL;
  jstring path = NULL; // 根据需要创建有效的 jstring
  jlong size = 0; // 根据实际需求赋值

  // 调用 JNI 方法
  jobjectArray result = Java_com_litongjava_media_NativeMedia_splitMp3(env, clazz, path, size);

  // 释放 JVM 资源
  (*jvm)->DestroyJavaVM(jvm);
  return 0;
}
