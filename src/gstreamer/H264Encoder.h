#pragma once

#include <gst/app/gstappsink.h>
#include <gst/app/gstappsrc.h>
#include <gst/gst.h>
#include <gst/video/video.h>

#include <atomic>
#include <cstdint>
#include <functional>

#include <opencv2/core.hpp>

// ----------------------------------------------------------------------------
// H264Encoder
//
// 화면에 표시 중인 BGR cv::Mat 한 장을 받아 H.264 access unit 스트림으로
// 인코딩하고, 콜백으로 한 access unit(=한 프레임) 씩 흘려주는 클래스.
//
// 내부 GStreamer 파이프라인:
//
//   appsrc → videoconvert → x264enc → h264parse(byte-stream, alignment=au) → appsink
//
//   * appsrc      : pushFrame() 으로 들어온 BGR 프레임을 입력으로 흘린다.
//   * videoconvert: BGR → 인코더가 받는 I420 등으로 픽셀 포맷 변환.
//   * x264enc     : 저지연 옵션(zerolatency) 의 소프트웨어 H.264 인코더.
//   * h264parse   : "stream-format=byte-stream, alignment=au" 강제 →
//                   appsink 가 받는 buffer 1개 = access unit 1개
//                   (한 프레임의 모든 NAL 이 Annex-B start code 와 함께 연속).
//   * appsink     : 콜백에서 access unit 을 꺼내 setNalCallback 으로 등록된
//                   콜백에 전달.
//
// access unit 단위로 전달하는 이유: RTSP 서버 쪽 rtph264pay 가 RTP marker bit
// 를 정확히 세팅하려면 한 frame 의 모든 NAL 이 한 buffer 로 묶여 있어야 한다.
// per-NAL 로 보내면 같은 PTS 의 buffer 가 여러 개 생기고, 원격 디코더에서
// 영상이 깨지는 현상이 발생한다.
//
// pushFrame 에 들어오는 cv::Mat 의 width/height 가 생성자에 준 값과 다르면
// 학습용 단순화로 그냥 무시한다.
// ----------------------------------------------------------------------------
class H264Encoder {
public:
    // 한 access unit(=한 프레임) 을 콜백으로 전달할 때 쓰는 시그니처.
    // data 는 byte-stream 형식의 access unit (Annex-B start code 포함, 한 프레임의
    // 모든 NAL 이 [start code | NAL][start code | NAL]... 형태로 연속).
    using NalCallback = std::function<void(const uint8_t* data, size_t size)>;

    H264Encoder(int width, int height, int fps);
    ~H264Encoder();

    H264Encoder(const H264Encoder&) = delete;
    H264Encoder& operator=(const H264Encoder&) = delete;

    // 파이프라인을 PLAYING 상태로 만든다. 이후 pushFrame 호출 가능.
    bool start();

    // 파이프라인을 정지하고 모든 GStreamer 리소스를 해제한다.
    void stop();

    // BGR cv::Mat 한 프레임을 인코더에 push.
    // 사이즈가 안 맞거나 stop 상태면 조용히 무시.
    void pushFrame(const cv::Mat& bgr);

    // 인코더가 만들어내는 NAL 한 단위마다 호출될 콜백 등록.
    void setNalCallback(NalCallback cb) { this->nalCb_ = std::move(cb); }

    // x264enc 에 즉시 IDR(keyframe) 생성을 요청한다.
    // RTSP PLAY 수신 직후 호출하면 클라이언트가 GOP 중간에 연결해도 즉시 정상 화면을 볼 수 있다.
    void forceKeyframe();

private:
    // appsink 가 새 buffer 를 만들 때마다 호출되는 C 콜백.
    static GstFlowReturn onNewSample(GstAppSink* sink, gpointer userData);

    // appsink 에서 꺼낸 한 buffer(=access unit 1개) 를 nalCb_ 에 그대로 전달.
    void handleEncodedBuffer(const uint8_t* data, size_t size);

    int width_;
    int height_;
    int fps_;

    GstElement* pipeline_ = nullptr;
    GstElement* appsrc_   = nullptr;
    GstElement* appsink_  = nullptr;

    std::atomic<bool> running_ = false;
    uint64_t          frameIndex_    = 0;  // PTS 계산용 단조 증가 카운터
    uint32_t          forceKeyCount_ = 0;  // GstForceKeyUnit 이벤트 시퀀스 번호

    NalCallback nalCb_;
};
