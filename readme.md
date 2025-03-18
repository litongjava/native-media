# java-native-media

Audio-Visual Processing Optimization Library

## Project Background

In traditional Java audio-visual processing solutions, developers typically rely on calling the FFmpeg command-line tool to implement related functionalities. This approach has two notable pain points:

1. **Performance Overhead**: Inter-process calls generate additional overhead.
2. **Package Bloat**: Bundling FFmpeg binaries leads to a significant increase in package size.

This project uses JNI technology to allow Java to directly invoke a C language audio-visual processing library, with deep optimizations for specific scenarios. While ensuring core functionalities, it aims to:

- Significantly enhance processing performance (by avoiding inter-process communication)
- Effectively control package size (by integrating only the necessary codec modules)

## Core Features

### 1. MP3 Intelligent Fragmentation

- **Application Scenario**: Suitable for adapting to large language models with a 25MB file input limit.
- **Technical Features**:
    - Supports smart splitting by specified byte count.
    - Processes fragmentation while maintaining audio integrity.
    - Employs a memory-efficient shunting mechanism.

### 2. MP4 to MP3

- **Application Scenario**: Preprocessing for voice recognition in video content.
- **Technical Features**:
    - Efficient extraction of the audio track.
    - Maintains the original audio quality.

## Notes

This project focuses on vertical optimizations for specific scenarios and is not a general-purpose multimedia processing framework. The main differences from FFmpeg include:

- Implements only the core functions frequently used.
- Lacks a graphical user interface or filter extensions.
- Does not support processing unconventional codec formats.

## Development Details

- [java-native-media](https://www.tio-boot.com/zh/54_native-media/01.html)
- [JNI Getting Started Example](https://www.tio-boot.com/zh/54_native-media/02.html)
- [MP3 Splitting](https://www.tio-boot.com/zh/54_native-media/03.html)
- [MP4 to MP3](https://www.tio-boot.com/zh/54_native-media/04.html)
- [High-Quality MP3 Encoding with libmp3lame](https://www.tio-boot.com/zh/54_native-media/05.html)
- [Linux Compilation](https://www.tio-boot.com/zh/54_native-media/06.html)
- [macOS M2 Compilation](https://www.tio-boot.com/zh/54_native-media/07.html)
- [Loading Native Libraries from a JAR Package](https://www.tio-boot.com/zh/54_native-media/08.html)