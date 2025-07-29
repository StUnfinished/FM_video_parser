# FM_video_parser 项目说明

## 项目简介
本项目用于从 H.265/H.264 视频流中批量提取影像帧，并自动解析 SEI 数据，将外方位元素（如经纬度、高程、姿态角等）写入影像的 EXIF/XMP 元数据或保存为 CSV 文件，适用于无人机航摄、遥感等场景。

## 主要功能模块

### 1. sei_parser.h / sei_parser.cpp
- **功能**：实现 SEI NALU 的识别、解析和元数据结构（SEIMetadata）定义。
- **用途**：为其他模块提供 SEI 数据解析能力。

### 2. parse_sei_from_file.cpp
- **功能**：从 H.265/H.264 本地文件或 RTMP 流中批量提取 SEI NALU，解析外方位元素，并保存为 CSV 文件。
- **用法**：
  ```bash
  # 本地文件：
  ./parse_sei_from_file input.h265
  # RTMP流：
  ./parse_sei_from_file rtmp://your_stream_url
  # 例如： ./parse_sei_from_file rtmp://192.168.1.212:19935/live/uav
  # 输出 CSV 文件路径可在代码中自定义
  ```

### 3. get_image_from_stream.cpp
- **功能**：从本地视频文件或 RTMP 流中提取影像帧，每隔 x 帧保存一张图片，并将 CSV 中的外方位元素写入对应影像的 EXIF/XMP。
- **用法**：
  ```bash
  # 本地文件：
  ./get_image_from_stream input.h265 sei_meta.csv x [output_dir]
  # 例如： ./get_image_from_stream input.h265 pos_file.csv 25 output_images
  # RTMP流：
  ./get_image_from_stream rtmp://your_stream_url sei_meta.csv x [output_dir]
  # 例如： ./get_image_from_stream rtmp://192.168.1.212:19935/live/uav pos_file.csv 25 output_images
  ```

### 4. parser_and_tag.cpp
- **功能**：一体化流程。自动从本地 H.265 文件或 RTMP 流中提取 SEI 元数据并保存为 CSV，同时每隔 x 帧保存一张图片并写入 EXIF/XMP 元数据。
  - 支持本地文件和 RTMP 流自动识别。
  - RTMP流模式下，使用 FFmpeg API 实时提取 SEI 并顺序缓存，OpenCV 保存帧，帧与 SEI 顺序对齐写入 EXIF。
  - 本地文件模式下，流程与以往一致。
- **用法**：
  ```bash
  # 本地文件：
  ./parser_and_tag input.h265 sei_meta.csv x [output_dir]
  # 例如： ./parser_and_tag input.h265 pos_file.csv 25 output_images
  # RTMP流：
  ./parser_and_tag rtmp://your_stream_url sei_meta.csv x [output_dir]
  # 例如： ./parser_and_tag rtmp://192.168.1.212:19935/live/uav pos_file.csv 25 output_images
  # 结果：output.csv 保存所有 SEI 元数据，output_images/ 下为带有 EXIF 的影像帧（帧与 SEI 顺序对齐）
  ```
  - **同步方式说明**：帧与 SEI 按顺序一一对应（即第N个SEI用于第N个保存的影像帧EXIF写入）。

## 依赖环境
- OpenCV
- Boost (filesystem, system)
- Exiv2
- FFmpeg (libavformat, libavcodec, libavutil, libswscale)
- C++14 及以上

### FFmpeg安装方法
#### 安装步骤
```bash
# 下载最新静态版本（x86_64 Linux）
wget https://johnvansickle.com/ffmpeg/releases/ffmpeg-release-amd64-static.tar.xz
tar -xf ffmpeg-release-amd64-static.tar.xz
cd ffmpeg-*-static
sudo cp ffmpeg /usr/local/bin/
sudo cp ffprobe /usr/local/bin/
```
#### 验证安装：
```bash
ffmpeg -version
```
然后在编译你项目时**只链接 FFmpeg 的 .so/.a 文件或通过命令行调用解码器**。

## 编译方法
```bash
cd package/fmSEIGet
mkdir build && cd build
cmake ..
make -j
```

## 典型使用流程
1. **一体化处理（推荐）**
   ```bash
   # 本地文件：
   ./parser_and_tag input.h265 output.csv 25 output_images
   # RTMP流：
   ./parser_and_tag rtmp://your_stream_url output.csv 25 output_images
   # 结果：output.csv 保存所有 SEI 元数据，output_images/ 下为带有 EXIF 的影像帧（顺序对齐）
   ```
2. **分步处理**
   - 先提取 SEI 元数据：
     ```bash
     ./parse_sei_from_file input.h265
     # 得到 pos_file.csv
     ```
   - 再批量保存影像帧并写入 EXIF：
     ```bash
     ./get_image_from_stream input.h265 pos_file.csv 25 output_images
     ```

## 代码结构说明
- `sei_parser.h/cpp`：SEI 解析核心
- `parse_sei_from_file.cpp`：SEI 批量提取
- `get_image_from_stream.cpp`：影像帧提取+EXIF写入
- `parser_and_tag.cpp`：SEI批量提取+影像帧提取+EXIF写入 一体化流程
- `CMakeLists.txt`：项目构建配置

## 注意事项
- 需保证依赖库已正确安装。
- 输出目录需有写权限。
- 若遇到 <filesystem> 相关报错，可用 boost::filesystem 替代。

---
如有问题请查阅源码注释或联系开发者。
