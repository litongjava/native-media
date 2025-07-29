#ifndef NATIVE_MEDIA_NATIVE_MP3_H
#define NATIVE_MEDIA_NATIVE_MP3_H

#include <stdlib.h>

/**
 * 将 input_file 对应的 MP4 文件转为 MP3，结果写到 output_file。
 *
 * @param input_file        输入 MP4 的路径（UTF-8 编码）
 * @param output_file       输出 MP3 的路径（UTF-8 编码）
 */
char *convert_to_mp3(const char *input_file, const char *output_file);
#endif //NATIVE_MEDIA_NATIVE_MP3_H
