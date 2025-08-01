# native-media

音视频处理优化库

## 项目背景

在传统 Java 音视频处理方案中，开发者通常依赖调用 FFmpeg 命令行工具实现相关功能。这种方式存在两个显著痛点：

1. **性能损耗**：进程间调用产生额外开销
2. **体积膨胀**：需捆绑 FFmpeg 二进制文件导致包体积剧增

本项目通过 JNI 技术实现 Java 直接调用 C 语言音视频处理库，针对特定场景进行深度优化，在保证核心功能的前提下：

- 显著提升处理性能（避免进程间通信）
- 有效控制包体积（仅集成必要编解码模块）

## 核心功能

### 1. MP3 智能分片

- **应用场景**：适配大语言模型 25MB 文件输入限制
- **技术特点**：
    - 支持按指定字节数智能分割
    - 保持音频完整性的分片处理
    - 内存高效的分流处理机制

### 2. MP4 转 MP3

- **应用场景**：视频内容语音识别预处理
- **技术特点**：
    - 高效提取音频轨道
    - 保持原始音频质量
## 常用测试命令

```
media to_mp3 tts-kokoro-en.wav tts-kokoro-en.mp3
media to_mp3 539850349333278720.mp4 539850349333278720.mp3
```
## 注意事项

本项目专注特定场景的垂直优化，并非通用型多媒体处理框架。与 FFmpeg 的主要差异：

- 仅实现高频使用场景的核心功能
- 无图形化界面/滤镜等扩展功能
- 不支持非常规编码格式处理


## 开发细节
- [java-native-media](https://www.tio-boot.com/zh/54_native-media/01.html)
- [JNI 入门示例](https://www.tio-boot.com/zh/54_native-media/02.html)
- [mp3拆分](https://www.tio-boot.com/zh/54_native-media/03.html)
- [mp4转mp3](https://www.tio-boot.com/zh/54_native-media/04.html)
- [使用 libmp3lame 实现高质量 MP3 编码](https://www.tio-boot.com/zh/54_native-media/05.html)
- [linux 编译](https://www.tio-boot.com/zh/54_native-media/06.html)
- [macOS M2编译](https://www.tio-boot.com/zh/54_native-media/07.html)
- [从 JAR 包中加载本地库文件](https://www.tio-boot.com/zh/54_native-media/08.html)