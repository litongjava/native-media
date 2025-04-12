#ifndef NATIVE_MEDIA_H
#define NATIVE_MEDIA_H

#ifdef __cplusplus
extern "C" {
#endif
typedef struct HlsSession HlsSession;

/**
 * @brief Splits the specified video file into HLS segments.
 *
 * This function uses FFmpeg to convert a video file (e.g., MP4, MKV, MOV, AVI, etc.) into HLS segments.
 * The generated TS segment files are stored in the same directory as the HLS playlist file.
 * The tsPattern parameter defines the naming template for the TS segments (e.g., "segment_%03d.ts").
 *
 * @param playlistUrl     Full path to the HLS playlist file where the HLS segments will be appended.
 * @param inputVideoPath  Full path to the input video file.
 * @param tsPattern       Naming template (including directory) for TS segment files.
 * @param segmentDuration Duration of each TS segment in seconds.
 *
 * @return A constant char pointer to a message indicating the result of HLS segmentation,
 *         e.g., a success message or an error description.
 */
const char *split_video_to_hls(const char *playlistUrl,
                               const char *inputVideoPath,
                               const char *tsPattern,
                               int segmentDuration);

/**
 * 初始化 HLS 持久化会话
 * @param playlistUrl 输出播放列表文件路径（例如 "./data/hls/test/master.m3u8"）
 * @param tsPattern TS 分段文件命名模板（例如 "./data/hls/test/segment_%03d.ts"）
 * @param startNumber 起始分段编号
 * @param segDuration 分段时长（秒）
 * @return 成功返回 HlsSession 指针，失败返回 NULL
 */
HlsSession *init_persistent_hls(const char *playlistUrl, const char *tsPattern, int startNumber, int segDuration);

/**
 * 追加一个 MP4 分段到指定 HLS 会话中
 * @param session 已初始化的 HlsSession 指针
 * @param inputFilePath 输入 MP4 文件的路径
 * @return 成功返回 0，失败返回负错误码
 */
int append_video_segment_to_hls(HlsSession *session, const char *inputFilePath);

/**
 * 结束 HLS 会话，写入 trailer 并释放资源
 * @param session 已初始化的 HlsSession 指针
 * @return 成功返回 0，失败返回负错误码
 */
int finish_prersistent_hls(HlsSession *session);

#ifdef __cplusplus
}
#endif

#endif // NATIVE_MEDIA_H
