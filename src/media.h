#ifndef NATIVE_MEDIA_H
#define NATIVE_MEDIA_H

#include <libavformat/avformat.h>
#include <time.h>

typedef struct HlsSession {
  AVFormatContext *ofmt_ctx;    // HLS muxer 输出上下文
  int segDuration;              // 分段时长（秒）
  int64_t global_offset;        // 全局时间戳偏移量 (微秒)
  char *ts_pattern;             // TS 分段命名模板
  int header_written;           // 是否已写 header
  AVDictionary *opts;           // HLS 选项
  time_t created_time;
  int closed;                   // 是否已关闭
} HlsSession;

// 初始化 HLS 会话
HlsSession *init_hls_session(const char *playlistUrl, const char *tsPattern, int startNumber, int segDuration);

// 追加视频片段 (核心修复逻辑在此)
char *append_video_segment(HlsSession *session, const char *inputFilePath);

// 结束 HLS 会话
char *finish_hls_session(HlsSession *session, const char *playlistUrl);

// 释放会话内存（不涉及文件操作，纯内存清理）
void free_hls_session_struct(HlsSession *session);

/**
 * 返回 0 成功，其它为 FFmpeg 错误码
 * @param inputPath
 * @param outputPath
 * @return
 */
int save_last_frame_c(const char *inputPath, const char *outputPath);

#endif // NATIVE_MEDIA_H