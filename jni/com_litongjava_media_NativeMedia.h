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
 * Method:    convertTo
 * Signature: (Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;
 */
JNIEXPORT jstring JNICALL Java_com_litongjava_media_NativeMedia_convertTo
  (JNIEnv *, jclass, jstring, jstring);

/*
 * Class:     com_litongjava_media_NativeMedia
 * Method:    split
 * Signature: (Ljava/lang/String;J)[Ljava/lang/String;
 */
JNIEXPORT jobjectArray JNICALL Java_com_litongjava_media_NativeMedia_split
  (JNIEnv *, jclass, jstring, jlong);

/*
 * Class:     com_litongjava_media_NativeMedia
 * Method:    supportFormats
 * Signature: ()[Ljava/lang/String;
 */
JNIEXPORT jobjectArray JNICALL Java_com_litongjava_media_NativeMedia_supportFormats
  (JNIEnv *, jclass);

/*
 * Class:     com_litongjava_media_NativeMedia
 * Method:    splitVideoToHLS
 * Signature: (Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;I)Ljava/lang/String;
 */
JNIEXPORT jstring JNICALL Java_com_litongjava_media_NativeMedia_splitVideoToHLS
  (JNIEnv *, jclass, jstring, jstring, jstring, jint);

/*
 * Class:     com_litongjava_media_NativeMedia
 * Method:    initPersistentHls
 * Signature: (Ljava/lang/String;Ljava/lang/String;II)J
 */
JNIEXPORT jlong JNICALL Java_com_litongjava_media_NativeMedia_initPersistentHls
  (JNIEnv *, jclass, jstring, jstring, jint, jint);

/*
 * Class:     com_litongjava_media_NativeMedia
 * Method:    appendVideoSegmentToHls
 * Signature: (JLjava/lang/String;)Ljava/lang/String;
 */
JNIEXPORT jstring JNICALL Java_com_litongjava_media_NativeMedia_appendVideoSegmentToHls
  (JNIEnv *, jclass, jlong, jstring);

/*
 * Class:     com_litongjava_media_NativeMedia
 * Method:    insertSilentSegment
 * Signature: (JD)Ljava/lang/String;
 */
JNIEXPORT jstring JNICALL Java_com_litongjava_media_NativeMedia_insertSilentSegment
  (JNIEnv *, jclass, jlong, jdouble);

/*
 * Class:     com_litongjava_media_NativeMedia
 * Method:    finishPersistentHls
 * Signature: (JLjava/lang/String;)Ljava/lang/String;
 */
JNIEXPORT jstring JNICALL Java_com_litongjava_media_NativeMedia_finishPersistentHls
  (JNIEnv *, jclass, jlong, jstring);

/*
 * Class:     com_litongjava_media_NativeMedia
 * Method:    merge
 * Signature: ([Ljava/lang/String;Ljava/lang/String;)Z
 */
JNIEXPORT jboolean JNICALL Java_com_litongjava_media_NativeMedia_merge
  (JNIEnv *, jclass, jobjectArray, jstring);

#ifdef __cplusplus
}
#endif
#endif
