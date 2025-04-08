#ifndef NATIVE_MEDIA_H
#define NATIVE_MEDIA_H

#ifdef __cplusplus
extern "C" {
#endif

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

#ifdef __cplusplus
}
#endif

#endif // NATIVE_MEDIA_H
