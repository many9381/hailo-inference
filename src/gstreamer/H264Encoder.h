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
// Class that takes a displayed BGR cv::Mat frame, encodes it into an H.264
// access-unit stream, and emits one access unit (= one frame) at a time through a callback.
//
// Internal GStreamer pipeline:
//
//   appsrc → videoconvert → x264enc → h264parse(byte-stream, alignment=au) → appsink
//
//   * appsrc      : feeds BGR frames received through pushFrame().
//   * videoconvert: converts the pixel format from BGR to what the encoder accepts, such as I420.
//   * x264enc     : software H.264 encoder with the low-latency zerolatency option.
//   * h264parse   : forces "stream-format=byte-stream, alignment=au" ->
//                   one buffer received by appsink = one access unit
//                   (all NALs of a frame in sequence with Annex-B start codes).
//   * appsink     : pulls an access unit in the callback and forwards it to
//                   the callback registered by setNalCallback().
//
// Why it is delivered per access unit: for rtph264pay on the RTSP server side
// to set the RTP marker bit correctly, all NALs of a frame must be grouped in one buffer.
// Sending per NAL creates multiple buffers with the same PTS and can cause corruption
// on remote decoders.
//
// If the cv::Mat passed to pushFrame has a width/height different from the constructor
// values, it is simply ignored as a simplification for this example.
// ----------------------------------------------------------------------------
class H264Encoder {
public:
    // Signature used when delivering one access unit (= one frame) through the callback.
    // data is a byte-stream access unit (including Annex-B start codes, with all NALs
    // of one frame laid out continuously as [start code | NAL][start code | NAL]...).
    using NalCallback = std::function<void(const uint8_t* data, size_t size)>;

    H264Encoder(int width, int height, int fps);
    ~H264Encoder();

    H264Encoder(const H264Encoder&) = delete;
    H264Encoder& operator=(const H264Encoder&) = delete;

    // Put the pipeline into the PLAYING state. pushFrame can be called afterward.
    bool start();

    // Stop the pipeline and release all GStreamer resources.
    void stop();

    // Push one BGR cv::Mat frame into the encoder.
    // Silently ignore it if the size does not match or the encoder is stopped.
    void pushFrame(const cv::Mat& bgr);

    // Register the callback invoked for each NAL unit produced by the encoder.
    void setNalCallback(NalCallback cb) { this->nalCb_ = std::move(cb); }

    // Request immediate IDR (keyframe) generation from x264enc.
    // If called right after receiving RTSP PLAY, a client joining mid-GOP can immediately see a valid image.
    void forceKeyframe();

private:
    // C callback invoked whenever appsink produces a new buffer.
    static GstFlowReturn onNewSample(GstAppSink* sink, gpointer userData);

    // Forward one buffer pulled from appsink (= one access unit) to nalCb_ as-is.
    void handleEncodedBuffer(const uint8_t* data, size_t size);

    int width_;
    int height_;
    int fps_;

    GstElement* pipeline_ = nullptr;
    GstElement* appsrc_   = nullptr;
    GstElement* appsink_  = nullptr;

    std::atomic<bool> running_ = false;
    uint64_t          frameIndex_    = 0;  // Monotonically increasing counter used for PTS calculation
    uint32_t          forceKeyCount_ = 0;  // GstForceKeyUnit event sequence number

    NalCallback nalCb_;
};
