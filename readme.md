# java-native-media

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

## 注意事项

本项目专注特定场景的垂直优化，并非通用型多媒体处理框架。与 FFmpeg 的主要差异：

- 仅实现高频使用场景的核心功能
- 无图形化界面/滤镜等扩展功能
- 不支持非常规编码格式处理


## 构建

```bash
apt-get update
apt-get install openjdk-11-jdk -y
```

同时，确保环境变量 `JAVA_HOME` 正确设置，例如：

```bash
export JAVA_HOME=/usr/lib/jvm/java-11-openjdk-amd64
```

安装完毕并设置好 `JAVA_HOME` 后，再次运行 CMake 配置命令，就应该能正确找到JNI，从而解决该错误。

用动态链接方式构建 ffmpeg（包括 libmp3lame 支持）
```shell
export VCPKG_LIBRARY_LINKAGE=dynamic
vcpkg install ffmpeg[mp3lame]:x64-linux-dynamic
```
或者使用系统包管理器
```shell
sudo apt-get update
sudo apt-get install libavcodec-dev libavformat-dev libavutil-dev libswresample-dev -y

```

```shell
cmake -S . -B cmake-build-release -DCMAKE_BUILD_TYPE=Release
cmake --build cmake-build-release --target all
```