#include <iostream>
#include <string>
#include <opencv2/opencv.hpp>
#include <unistd.h>
#include "SafeQueue.h"
#include <fstream>
#include <vector>
#include <opencv2/dnn.hpp>  // OpenCV 的 DNN 模块，用于加载和运行深度学习模型
#include <thread>


extern "C" {
    #include <libavformat/avformat.h>
    #include <libavcodec/avcodec.h>
    #include <libavutil/avutil.h>
    #include <libavutil/time.h>
    #include <libavutil/opt.h>
    #include <libswscale/swscale.h>
    #include <libavutil/imgutils.h>
}

using namespace std;
using namespace cv;
using namespace dnn;    // OpenCV 的 DNN 模块命名空间

SafeQueue<AVPacket*> packet_queue;

void network_push_thread(AVFormatContext* ofmt_ctx, AVRational enc_time_base, AVRational out_time_base, int out_stream_index){
    cout << "网络发送线程已启动，等待数据" << endl;

    long long start_time = av_gettime();

    while (true) {

        AVPacket* pkt = packet_queue.pop();

        // 控速发包（以编码后的包为准）
        if (pkt->pts != AV_NOPTS_VALUE) {

            int64_t pts_us = av_rescale_q(pkt->pts, enc_time_base, {1, AV_TIME_BASE});  // 时间单位换算， 把编码器的时间基换算成微秒
            int64_t now_us = av_gettime() - start_time;

            if (pts_us >= now_us) {
                usleep(pts_us - now_us);
            }

        }
        
        // 时间单位换算为RTSP单位
        if (pkt->pts != AV_NOPTS_VALUE) {
            pkt->pts = av_rescale_q(pkt->pts, enc_time_base, out_time_base);
        }
        
        if (pkt->dts != AV_NOPTS_VALUE) {
            pkt->dts = av_rescale_q(pkt->dts, enc_time_base, out_time_base);
        }
        
        if (pkt->duration > 0) {
            pkt->duration = av_rescale_q(pkt->duration, enc_time_base, out_time_base);
        }
        
        pkt->stream_index = out_stream_index;  // 流索引, 0 表示视频, 1 表示音频

        // 发给 ZLMediaKit
        av_write_frame(ofmt_ctx, pkt);

        av_packet_free(&pkt);   // 释放 AVPacket
    }
    
}

int main() {

    const char* in_filename = "../都江堰.mp4";
    const char* out_url = "rtsp://127.0.0.1:554/live/ai_camera";

    cout << "加载yolo模型..." << endl;

    vector<string> classes;
    ifstream ifs("../coco.names");  // ifstream 用于读取文件，构造函数参数是文件路径, 打开文件后 ifs 就是一个输入流对象，可以用它来读取文件内容

    string line;
    while(getline(ifs, line)) { // getline函数从输入流ifs中读取一行文本，存储在字符串line中，直到文件末尾
        classes.push_back(line);   // 把每一行的内容（类别名称）添加到 classes 向量中
    }

    Net net = readNetFromDarknet("../yolov4-tiny.cfg", "../yolov4-tiny.weights");  // 加载YOLOv4-tiny模型，cfg文件包含网络结构，weights文件包含预训练权重

    net.setPreferableBackend(DNN_BACKEND_OPENCV);  // 设置计算后端为OpenCV
    net.setPreferableTarget(DNN_TARGET_CPU);       // 设置计算设备为CPU

    vector<string> outNames = net.getUnconnectedOutLayersNames();  // 获取输出层的名称，YOLO模型的输出层通常是检测结果所在的层

    cout << "模型加载成功" << endl;

    AVFormatContext* ifmt_ctx = nullptr;

    avformat_open_input(&ifmt_ctx, in_filename, nullptr,nullptr);

    avformat_find_stream_info(ifmt_ctx,nullptr);

    int video_index_streams = 0;
    for (unsigned int i = 0; i < ifmt_ctx->nb_streams; i++) {
        if (ifmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_index_streams = i;
            break;
        }
    }

    AVStream* in_stream = ifmt_ctx->streams[video_index_streams];

    AVCodec* decoder = avcodec_find_decoder(in_stream->codecpar->codec_id);
    AVCodecContext* dec_ctx = avcodec_alloc_context3(decoder);

    avcodec_parameters_to_context(dec_ctx, in_stream->codecpar);
    avcodec_open2(dec_ctx, decoder, nullptr);

    AVFormatContext* ofmt_ctx = nullptr;
    avformat_alloc_output_context2(&ofmt_ctx, nullptr, "rtsp", out_url);

    // 解码器
    AVCodec* encoder = avcodec_find_encoder(AV_CODEC_ID_H264);
    AVCodecContext* enc_ctx = avcodec_alloc_context3(encoder);

    enc_ctx->width = dec_ctx->width;
    enc_ctx->height = dec_ctx->height;
    enc_ctx->time_base = {1, 30};   // 时间刻度，一个刻度等于 1/30 秒
    enc_ctx->framerate = {30, 1};   // 规定真实的帧率 30帧/秒
    enc_ctx->pix_fmt = AV_PIX_FMT_YUV420P;  // 像素格式

    av_opt_set(enc_ctx->priv_data, "preset", "ultrafast", 0);   // 牺牲压缩率，加快速度编码
    av_opt_set(enc_ctx->priv_data, "tune", "zerolatency", 0);   // 零延迟，拿到一帧就吐这一帧，不缓存

    avcodec_open2(enc_ctx, encoder, nullptr);

    AVStream* out_stream = avformat_new_stream(ofmt_ctx, nullptr);
    avcodec_parameters_from_context(out_stream->codecpar,enc_ctx);  // 从编码器上下文复制参数到输出流，告诉ZLMediaKit，这个是H264
    out_stream->time_base = enc_ctx->time_base;

    // 连网线，开启tcp传输，写RTSP头
    AVDictionary* opt = nullptr;
    av_dict_set(&opt, "rtsp_transport", "tcp", 0);  // 开启tcp传输  键/值
    avio_open(&ofmt_ctx->pb, out_url, AVIO_FLAG_WRITE); // 打开输出文件
    avformat_write_header(ofmt_ctx, &opt);  // out_stream 的time_base被改成RTSP协议的时间刻度
    av_dict_free(&opt); // 释放字典

    // 初始化调色板 swscontext
    // YUV -> BGR 
    SwsContext* sws_yuv2bgr = sws_getContext(
        dec_ctx->width, dec_ctx->height, dec_ctx->pix_fmt,
        dec_ctx->width, dec_ctx->height,AV_PIX_FMT_BGR24,
        SWS_BILINEAR, nullptr, nullptr, nullptr
    );

    SwsContext* sws_bgr2yuv = sws_getContext(
        dec_ctx->width, dec_ctx->height, AV_PIX_FMT_BGR24,
        enc_ctx->width, enc_ctx->height, enc_ctx->pix_fmt,
        SWS_BILINEAR, nullptr, nullptr, nullptr
    );

    AVPacket* pkt_in = av_packet_alloc();
    AVPacket* pkt_out = av_packet_alloc();

    AVFrame* frame_yuv = av_frame_alloc();  // 解码出来的纯净YUV画面
    AVFrame* frame_bgr = av_frame_alloc();  // 准备交给OPENCV 的BGR画面
    AVFrame* frame_yuv_out = av_frame_alloc(); // 画完框，准备去编码的YUV画面

    // 给 frame_bgr 分配真实内存
    int bgr_size = av_image_get_buffer_size(AV_PIX_FMT_BGR24, dec_ctx->width, dec_ctx->height, 1);
    uint8_t* bgr_buffer = (uint8_t*) av_malloc(bgr_size);
    // 把内存填充到 frame_bgr，data指针指向buffer的正确位置
    av_image_fill_arrays(frame_bgr->data, frame_bgr->linesize, bgr_buffer, AV_PIX_FMT_BGR24, dec_ctx->width, dec_ctx->height, 1);

    // 给 frame_yuv_out 分配真实内存
    int yuv_size = av_image_get_buffer_size(enc_ctx->pix_fmt, enc_ctx->width, enc_ctx->height, 1);
    uint8_t* yuv_buffer = (uint8_t*) av_malloc(yuv_size);
    av_image_fill_arrays(frame_yuv_out->data, frame_yuv_out->linesize, yuv_buffer, enc_ctx->pix_fmt, enc_ctx->width, enc_ctx->height, 1);

    frame_yuv_out->width = enc_ctx->width;
    frame_yuv_out->height = enc_ctx->height;
    frame_yuv_out->format = enc_ctx->pix_fmt;

    cout << "生产车间已就绪！" << out_url << endl;

    std::thread push_thread(network_push_thread, ofmt_ctx, enc_ctx->time_base, out_stream->time_base, out_stream->index);
    push_thread.detach();

    int frame_count = 0;
    
    long long start_time_absolute = av_gettime(); 
    // 生产者循环：解码，画框，编码，放入传送带
    // 开始流水线
    while (true){

        if (av_read_frame(ifmt_ctx, pkt_in) < 0) {

            cout << "读取完毕" << endl;
            avio_seek(ifmt_ctx->pb, 0, SEEK_SET);   // 物理指针回到文件头
            avformat_seek_file(ifmt_ctx, -1, 0, 0, 0, 0);   // 逻辑指针回到文件头

            avcodec_flush_buffers(dec_ctx);   // 刷新解码器内部缓冲区，丢弃之前的状态，准备重新解码
            continue;
        }

        if (pkt_in->stream_index == video_index_streams) {
            // 送去解码
            if (avcodec_send_packet(dec_ctx, pkt_in) >= 0) {

                while (avcodec_receive_frame(dec_ctx, frame_yuv) >= 0) {

                    sws_scale(sws_yuv2bgr, frame_yuv->data, frame_yuv->linesize, 0, dec_ctx->height, frame_bgr->data, frame_bgr->linesize);
                    
                    Mat img(dec_ctx->height, dec_ctx->width, CV_8UC3, frame_bgr->data[0], frame_bgr->linesize[0]);
                    
                    Mat blob = blobFromImage(img, 1.0/255.0, Size(416, 416), Scalar(0, 0, 0), true, false); // 预处理，缩放到416x416，交换RB通道，归一化, 得到一个4维的blob纯净张量，尺寸是1x3x416x416

                    net.setInput(blob);  // 把blob送入网络

                    vector<Mat> outs;


                    vector<int> classIds;   // 装“物体的身份编号”（比如 0代表人类，2代表汽车）

                    vector<float> confidences;  // 装“AI 的确定程度”（比如 0.98，代表 98% 确定）

                    vector<Rect> boxes; // 装“物体在画面上的具体位置”（左上角坐标 + 宽高）

                    for (int i = 0; i < outs.size(); i++) {

                        float* data = (float*) outs[i].data;  // 当前输出层的指针，指向一个连续的内存块，里面存储了所有检测结果的数据, 物理上是连续的， OpenCV 默认是 uchar* 类型，所以要强转成 float*，才能正确访问每个检测结果的数值

                        for (int j = 0; j < outs[i].rows; j++, data += outs[i].cols) {
                            Mat scores(1, outs[i].cols - 5, CV_32FC1, data + 5);  // scores()  ,得到当前检测结果的类别分数，前5列是框的位置和大小，后面是各个类别的分数

                            Point classIdPoint; // 用来记录那个最高分，是属于哪个物体的
                            double confidence;  //用来记录当前这一行里，最高的那个得分

                            minMaxLoc(scores, 0, &confidence, 0, &classIdPoint); // 找到分数最高的类别和对应的分数, minmaxloc找最大值和最小值

                            if (confidence > 0.5) {
                                int centerX = (int)(data[0] * img.cols);
                                int centerY = (int)(data[1] * img.rows);
                                int width = (int)(data[2] * img.cols);
                                int height = (int)(data[3] * img.rows);

                                int left = centerX - width / 2;
                                int top = centerY - height / 2;

                                classIds.push_back(classIdPoint.x); // 
                                confidences.push_back((float) confidence);
                                boxes.push_back(Rect(left, top, width, height));
                            
                            }
                        }
                    }

                    // NMS，非极大值抑制，去掉重叠框
                    vector<int> indices;
                    NMSBoxes(boxes, confidences, 0.5, 0.4, indices);    // boxes是所有框，confidences是所有框的置信度，0.5是置信度阈值，0.4是NMS的IoU阈值，indices是保留下来的框的索引

                    for (size_t i = 0; i < indices.size(); i++) {
                        int idx = indices[i];
                        Rect box = boxes[idx];

                        rectangle(img, box, Scalar(0, 255, 0), 2);

                        string label = classes[classIds[idx]]+ ":" + format("%.2f", confidences[idx]);
                        putText(img, label, Point(box.x, box.y - 5), FONT_HERSHEY_SIMPLEX, 0.5, Scalar(0, 255, 0), 1); // 在框的左上角显示置信度
                    }

                    putText(img, "YOLOv4-tiny:" + to_string(frame_count), Point(20, 40), FONT_HERSHEY_SIMPLEX, 0.8, Scalar(0, 0, 255), 2); // 在画面左上角显示模型名称

                    sws_scale(sws_bgr2yuv, frame_bgr->data, frame_bgr->linesize, 0, dec_ctx->height, frame_yuv_out->data, frame_yuv_out->linesize); // 把BGR图像转换成YUV图像

                    // 时间戳
                    //frame_yuv_out->pts = frame_count;   // pts 表示在第几个刻度显示画面

                    // 1. 看看现实世界从程序启动到现在，到底流逝了多少微秒？
                    int64_t real_now_us = av_gettime() - start_time_absolute;

                    // 2. 把真实世界的微秒，换算成编码器的刻度，死死盖在画面上！
                    frame_yuv_out->pts = av_rescale_q(real_now_us, {1, AV_TIME_BASE}, enc_ctx->time_base);

                    if (avcodec_send_frame(enc_ctx, frame_yuv_out) >= 0) {
                        while(avcodec_receive_packet(enc_ctx, pkt_out) >=0) {

                            AVPacket* clone_pkt = av_packet_clone(pkt_out);
                            packet_queue.push(clone_pkt);
                            av_packet_unref(pkt_out);
                        }   
                    }
                    frame_count++;
                }
            }
        }
        av_packet_unref(pkt_in);    // 清理pkt的内存，避免内存泄露
        
    }
        
    av_write_trailer(ofmt_ctx);
    av_free(bgr_buffer);
    av_free(yuv_buffer);
    av_frame_free(&frame_bgr);
    av_frame_free(&frame_yuv);
    av_frame_free(&frame_yuv_out);
    sws_freeContext(sws_yuv2bgr);
    sws_freeContext(sws_bgr2yuv);
    av_packet_free(&pkt_in);
    av_packet_free(&pkt_out);
    avcodec_free_context(&dec_ctx);
    avcodec_free_context(&enc_ctx);
    avformat_close_input(&ifmt_ctx);
    avio_close(ofmt_ctx->pb);
    avformat_free_context(ofmt_ctx);

    cout << "流水线结束" << endl;    

    return 0;
}
