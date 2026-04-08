#include "RtspPusher.h"
#include <iostream>
#include <unistd.h> // for av_usleep

using namespace std;

RtspPusher::RtspPusher() {
    // 构造函数，初始化成员变量
}

RtspPusher::~RtspPusher() {
    // 析构函数，清理资源
    cleanup();
}

void RtspPusher::cleanup() {
    // 清理资源，关闭输入输出上下文，释放内存
    if (ifmt_ctx) {
        avformat_close_input(&ifmt_ctx); // 关闭输入文件，释放 ifmt_ctx 内部资源
        ifmt_ctx = nullptr; // 避免野指针
    }

    if (ofmt_ctx) {
        if (!(ofmt_ctx->oformat->flags & AVFMT_NOFILE)) { // 如果输出上下文存在，并且它的封装格式需要我们手动打开 I/O 句柄
            avio_close(ofmt_ctx->pb); // 关闭输出 I/O 句柄，释放资源
        }
        avformat_free_context(ofmt_ctx); // 释放输出上下文资源
        ofmt_ctx = nullptr; // 避免野指针
    }
}

bool RtspPusher::connect(const std::string& in_url, const std::string& out_url) {
    // 连接输入文件和输出 RTSP 服务器，初始化 ifmt_ctx 和 ofmt_ctx，找到视频流索引，准备好推流环境
    if (avformat_open_input(&ifmt_ctx, in_url.c_str(), 0, 0) < 0) {
        cerr << "无法打开输入文件" << endl;
        return false;
    }
    
    if (avformat_find_stream_info(ifmt_ctx, 0) < 0) {
        cerr << "无法获取流信息" << endl;
        return false;
    }

    // 找视频流索引
    for (unsigned int i = 0; i < ifmt_ctx->nb_streams; i++) {
        if (ifmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_index = i;
            break;
        }
    }

    avformat_alloc_output_context2(&ofmt_ctx, nullptr, "rtsp", out_url.c_str());
    if (!ofmt_ctx) {
        cerr << "无法创建输出上下文" << endl;
        return false;
    }

    for (unsigned int i = 0; i < ifmt_ctx->nb_streams; i++) {
        AVStream* in_stream = ifmt_ctx->streams[i];
        AVStream* out_stream = avformat_new_stream(ofmt_ctx, nullptr); // 传 nullptr，表示不指定编码器，让 FFmpeg 自动选择。out_stream 局部指针变量，和 ofmt_ctx->streams[i] 是同一个地址

        if (!out_stream) return false;

        avcodec_parameters_copy(out_stream->codecpar, in_stream->codecpar); // 复制编码参数, 原样搬运
        out_stream->codecpar->codec_tag = 0; // 某些封装格式要求必须设置为0，否则可能无法正确识别编码格式
    }

    if (!(ofmt_ctx->oformat->flags & AVFMT_NOFILE)) {

        if (avio_open(&ofmt_ctx->pb, out_url.c_str(), AVIO_FLAG_WRITE) < 0) {
            cerr << "无法连接 RTSP 服务器" << endl;
            return false;
        }
    } 

    // if (avformat_write_header(ofmt_ctx, nullptr) < 0) {
    //     cerr << "RTSP 握手失败" << endl;
    //     return false;
    // }

    AVDictionary* options = nullptr;
    av_dict_set(&options, "rtsp_transport", "tcp", 0);
    if (avformat_write_header(ofmt_ctx, &options) < 0) {
        cerr << "RTSP 握手失败" << endl;
        return false;
    }

    av_dict_free(&options); // 释放字典资源

    cout << "RTSP连接成功，准备推流" << endl;
    return true;
}

void RtspPusher::start() {
    // 开始推流，循环读取输入包，修正时间戳，写入输出上下文
    AVPacket pkt;
    start_time_absolute = av_gettime();

    int loop_count = 0; // 记录当前是第几次循环播放

    // 获取 MP4 文件的总时长 (单位是微秒 AV_TIME_BASE)
    int64_t file_duration_us = ifmt_ctx->duration; 
    if (file_duration_us <= 0) {
        file_duration_us = 5000000; // 如果读不到时长，兜底设为 5 秒 (5000000 微秒)
    }

    while (true)
    {
        if (av_read_frame(ifmt_ctx, &pkt) < 0) {
            loop_count++; // 循环次数 +1
            cout << "--- 触发循环播放，当前是第 " << loop_count + 1 << " 遍 ---" << endl;

            avio_seek(ifmt_ctx->pb, 0, SEEK_SET); // 重新定位到输入文件的开头，准备下一轮循环读取
            avformat_seek_file(ifmt_ctx, -1, 0, 0, 0, 0); // 重新定位所有流的时间戳，准备下一轮循环读取
            continue; // 继续下一轮循环 
        }

        int idx = pkt.stream_index;
        AVStream* in_stream = ifmt_ctx->streams[idx];
        AVStream* out_stream = ofmt_ctx->streams[idx];

        int64_t offset_pts = av_rescale_q(loop_count * file_duration_us, {1, AV_TIME_BASE}, in_stream->time_base);
        
        if (pkt.pts != AV_NOPTS_VALUE) {
            pkt.pts += offset_pts;
        }

        if (pkt.dts != AV_NOPTS_VALUE) {
            pkt.dts += offset_pts;
        }

        if (idx == video_index  && pkt.pts != AV_NOPTS_VALUE) {
            AVRational time_base = {1, AV_TIME_BASE};
            int64_t pts_time = av_rescale_q(pkt.pts, in_stream->time_base, time_base);
            int64_t now_time = av_gettime() - start_time_absolute;

            if (pts_time > now_time) {
                av_usleep(pts_time - now_time);
            }
        }

        if (pkt.pts != AV_NOPTS_VALUE) {
            pkt.pts = av_rescale_q_rnd(pkt.pts, in_stream->time_base, out_stream->time_base, (AVRounding)(AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));
        }
        if (pkt.dts != AV_NOPTS_VALUE) {
            pkt.dts = av_rescale_q_rnd(pkt.dts, in_stream->time_base, out_stream->time_base, (AVRounding)(AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));
        }
        if (pkt.duration > 0) {
            pkt.duration = av_rescale_q(pkt.duration, in_stream->time_base, out_stream->time_base);
        }
        pkt.pos = -1;

        if (av_interleaved_write_frame(ofmt_ctx, &pkt) < 0) {
            cerr << "写入数据包失败" << endl;
            break;
        }

        av_packet_unref(&pkt);
    }
    av_write_trailer(ofmt_ctx);
}
