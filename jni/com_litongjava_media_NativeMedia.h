/* DO NOT EDIT THIS FILE - it is machine generated */
#include <jni.h>
/* Header for class com_litongjava_media_NativeMedia */

#ifndef _Included_com_litongjava_media_NativeMedia
#define _Included_com_litongjava_media_NativeMedia
#ifdef __cplusplus
extern "C" {
#endif
/*
 * Class:     com_litongjava_media_NativeMedia
 * Method:    splitMp3
 * Signature: (Ljava/lang/String;J)[Ljava/lang/String;
 */
JNIEXPORT jobjectArray JNICALL Java_com_litongjava_media_NativeMedia_splitMp3
  (JNIEnv *, jclass, jstring, jlong);

/*
 * Class:     com_litongjava_media_NativeMedia
 * Method:    mp4ToMp3
 * Signature: (Ljava/lang/String;)Ljava/lang/String;
 */
JNIEXPORT jstring JNICALL Java_com_litongjava_media_NativeMedia_mp4ToMp3
  (JNIEnv *, jclass, jstring);

/*
 * Class:     com_litongjava_media_NativeMedia
 * Method:    toMp3
 * Signature: (Ljava/lang/String;)Ljava/lang/String;
 */
JNIEXPORT jstring JNICALL Java_com_litongjava_media_NativeMedia_toMp3
  (JNIEnv *, jclass, jstring);

/*
 * Class:     com_litongjava_media_NativeMedia
 * Method:    supportFormats
 * Signature: ()[Ljava/lang/String;
 */
JNIEXPORT jobjectArray JNICALL Java_com_litongjava_media_NativeMedia_supportFormats
  (JNIEnv *, jclass);

#ifdef __cplusplus
}
#endif
#endif
