#include "com_litongjava_media_NativeMedia.h"
#include <jni.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  int version;
  int layer;
  int bitrate;    // bps
  int samplerate;  // Hz
  int frame_length;
} MP3FrameInfo;

int parse_mp3_frame(const unsigned char *header, MP3FrameInfo *info) {
  // Verify sync word
  if (header[0] != 0xFF || (header[1] & 0xE0) != 0xE0) {
    return 0;
  }

  // MPEG version
  int version_bits = (header[1] >> 3) & 0x03;
  if (version_bits == 0x03) info->version = 3;  // MPEG-1
  else if (version_bits == 0x02) info->version = 2;  // MPEG-2
  else return 0;

  // Layer
  int layer_bits = (header[1] >> 1) & 0x03;
  if (layer_bits == 0x01) info->layer = 3;
  else return 0;

  // Bitrate index
  int bitrate_index = (header[2] >> 4) & 0x0F;
  const int bitrate_table[16] = {
    0,32,40,48,56,64,80,96,112,128,160,192,224,256,320,0
  };
  info->bitrate = bitrate_table[bitrate_index] * 1000;

  // Sample rate
  int samplerate_index = (header[2] >> 2) & 0x03;
  const int samplerate_table[4] = {44100, 48000, 32000, 0};
  info->samplerate = samplerate_table[samplerate_index];

  if (info->bitrate == 0 || info->samplerate == 0) return 0;

  // Calculate frame length
  info->frame_length = (144 * info->bitrate) / info->samplerate;
  if ((header[2] >> 1) & 0x01) info->frame_length++;  // Padding

  return 1;
}

JNIEXPORT jobjectArray JNICALL
Java_com_litongjava_media_NativeMedia_splitMp3(JNIEnv *env, jclass clazz, jstring srcPath, jlong size) {
  const char *src_path = (*env)->GetStringUTFChars(env, srcPath, NULL);
  if (!src_path) return NULL;

  // Prepare output filenames
  char base_path[1024];
  strncpy(base_path, src_path, sizeof(base_path)-1);
  base_path[sizeof(base_path)-1] = '\0';

  // Remove extension
  char *dot = strrchr(base_path, '.');
  if (dot && strcasecmp(dot, ".mp3") == 0) *dot = '\0';

  FILE *input = fopen(src_path, "rb");
  if (!input) {
    (*env)->ReleaseStringUTFChars(env, srcPath, src_path);
    return NULL;
  }

  // Skip ID3v2 tag
  unsigned char id3_header[10];
  if (fread(id3_header, 1, 10, input) == 10 &&
      memcmp(id3_header, "ID3", 3) == 0) {
    long tag_size = (id3_header[6] << 21) | (id3_header[7] << 14) |
                    (id3_header[8] << 7) | id3_header[9];
    fseek(input, tag_size + 10, SEEK_SET);
  } else {
    fseek(input, 0, SEEK_SET);
  }

  int split_count = 0;
  size_t current_size = 0;
  FILE *output = NULL;
  char output_path[1024];
  const size_t max_size = (size_t)size;

  // Frame reading buffer
  unsigned char header[4];
  MP3FrameInfo frame_info;

  while (fread(header, 1, 4, input) == 4) {
    if (!parse_mp3_frame(header, &frame_info)) {
      fseek(input, -3, SEEK_CUR);
      continue;
    }

    if (frame_info.frame_length <= 4) {
      fseek(input, -3, SEEK_CUR);
      continue;
    }

    if (!output || current_size + frame_info.frame_length > max_size) {
      if (output) fclose(output);
      split_count++;
      snprintf(output_path, sizeof(output_path), "%s_part%d.mp3", base_path, split_count);
      output = fopen(output_path, "wb");
      if (!output) break;
      current_size = 0;
    }

    fseek(input, -4, SEEK_CUR);
    unsigned char *frame = malloc(frame_info.frame_length);
    if (!frame || fread(frame, 1, frame_info.frame_length, input) != frame_info.frame_length) {
      free(frame);
      break;
    }

    fwrite(frame, 1, frame_info.frame_length, output);
    free(frame);
    current_size += frame_info.frame_length;
  }

  if (output) fclose(output);
  fclose(input);
  (*env)->ReleaseStringUTFChars(env, srcPath, src_path);

  // Build result array
  jclass stringClass = (*env)->FindClass(env, "java/lang/String");
  jobjectArray result = (*env)->NewObjectArray(env, split_count, stringClass, NULL);

  for (int i = 0; i < split_count; i++) {
    char path[1024];
    snprintf(path, sizeof(path), "%s_part%d.mp3", base_path, i+1);
    jstring str = (*env)->NewStringUTF(env, path);
    (*env)->SetObjectArrayElement(env, result, i, str);
    (*env)->DeleteLocalRef(env, str);
  }

  return result;
}