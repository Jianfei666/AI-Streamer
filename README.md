# AI-Streamer: 高性能 C++ 边缘 AI 视频流媒体网关 🚀

![C++](https://img.shields.io/badge/C++-11%2B-blue.svg)
![FFmpeg](https://img.shields.io/badge/FFmpeg-4.x-green.svg)
![OpenCV](https://img.shields.io/badge/OpenCV-4.x-red.svg)
![WebRTC](https://img.shields.io/badge/WebRTC-Ultra%20Low%20Latency-orange.svg)

## 📖 项目简介
本项目是一个基于 **C++11、FFmpeg 与 OpenCV** 构建的工业级流媒体 AI 视觉处理网关。
旨在解决传统安防 IPC（网络摄像机）与边缘计算盒子中，**AI 算法推理耗时与网络推流阻塞**之间的资源竞争痛点。

通过引入自主设计的 **线程安全队列 (SafeQueue)** 与 **生产者-消费者并发模型**，本项目成功将视频解码、YOLO 目标检测渲染、H.264 重编码与 RTSP/WebRTC 网络推流彻底解耦，实现了 **7x24 小时高吞吐、绝对音画同步、毫秒级超低延迟** 的 AI 视频流分发。

## ✨ 核心特性 (Core Features)
- **⚡ 多线程并发解耦**：自研基于 `std::mutex` 与 `std::condition_variable` 的防虚假唤醒线程安全队列，彻底分离 AI 计算与网络 I/O。
- **👁️ OpenCV 零拷贝渲染 (Zero-Copy)**：利用 `sws_scale` 将解码后的 YUV 数据直接映射至 `cv::Mat` 内存空间，省去极其耗时的内存深拷贝，极大压榨 CPU 性能。
- **🧠 边缘端 YOLOv4-tiny 部署**：集成 OpenCV DNN 模块，实现了视频流中 80 种目标的实时检测、NMS 非极大值抑制过滤与 OSD 动态边框绘制。
- **⏱️ 绝对物理时钟戳同步**：废弃依赖输入流时间戳的传统做法，采用基于 `av_gettime()` 的现实世界绝对时钟打戳算法 (`av_rescale_q`)，完美解决流媒体无限循环推流时的时钟撕裂与断流问题。
- **🌐 拥抱 WebRTC 超低延迟**：配合 ZLMediaKit 流媒体服务器，将传统 RTSP 流无缝转封装 (Remux) 为 WebRTC 协议，实现浏览器端 **<100ms** 的极致低延迟播放。

## 🏗️ 系统架构设计
数据流在本项目中经历了极其严密的生命周期管理与格式转换：
`[MP4 视频流]` -> `Demux (解封装)` -> `Decode (H.264 解码)` -> `SwsContext (YUV转BGR)` -> `OpenCV DNN (YOLO 推理与画框)` -> `SwsContext (BGR转YUV)` -> `Encode (H.264 重编码, zerolatency)` -> `Mux (封包)` -> `TCP 发送至 ZLMediaKit` -> `WebRTC 网页秒开`。

## 🛠️ 环境依赖
* **OS**: Ubuntu 20.04 / 22.04
* **Compiler**: GCC/G++ (支持 C++11)
* **Build**: CMake >= 3.10
* **Libraries**: 
  * `libavformat-dev`, `libavcodec-dev`, `libswscale-dev`, `libavutil-dev` (FFmpeg)
  * `libopencv-dev` (OpenCV 4.x)
* **Media Server**:[ZLMediaKit](https://github.com/ZLMediaKit/ZLMediaKit) (需开启 WebRTC 支持)

## 🚀 编译与运行
### 1. 准备 YOLOv4-tiny 模型
请将以下文件下载至 `models/` 目录下：
* `yolov4-tiny.cfg`
* `yolov4-tiny.weights`
* `coco.names`

### 2. 编译项目
```bash
mkdir build && cd build
cmake ..
make -j8

### 3. 运行流水线
请确保 ZLMediaKit 已在后台启动监听 554 端口：
./ai_camera
随后在 VLC 或 Chrome 浏览器 (WebRTC 模式) 中打开 rtsp://127.0.0.1:554/live/ai_camera 即可观看带有 AI 追踪框的实时直播流！
