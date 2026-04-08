#pragma once
#include <string>
#include <vector>

extern "C" { // 告诉编译器按照 C 的方式来链接这些 FFmpeg 的函数，避免 C++ 的名字修饰导致链接错误
    #include <libavformat/avformat.h> // 包含了 AVFormatContext、AVStream 等结构体的定义，以及 avformat_open_input、avformat_find_stream_info、av_read_frame 等函数的声明
    #include <libavcodec/avcodec.h>
    #include <libavutil/avutil.h>
    #include <libavutil/time.h>
}

class RtspPusher {
public:
    RtspPusher();
    ~RtspPusher();

    // 连接输入文件和输出 RTSP 服务器
    bool connect(const std::string& in_url, const std::string& out_url);

    // 开始推流
    void start();

private:
    // 收尾
    void cleanup();

private:
    AVFormatContext* ifmt_ctx = nullptr; // 输入上下文
    AVFormatContext* ofmt_ctx = nullptr; // 输出上下文

    int video_index = -1; // 视频流索引
    long long start_time_absolute = 0; // 绝对开始时间，单位微秒

    // int64_t 是 FFmpeg 中表示时间戳的类型，单位是输入流的时间基。我们需要记录每个流的最大 PTS 和总偏移量来实现循环播放时的时间戳修正。
};
