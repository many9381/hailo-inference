#include "VideoPipeline.h"

#include <QDebug>

#include "GstBootstrap.h"

VideoPipeline::VideoPipeline(QObject* parent) : QObject(parent) {}

VideoPipeline::~VideoPipeline() {
    this->stop();
}

bool VideoPipeline::start(const std::string& filepath) {
    if (!initializeGStreamer()) {
        qWarning() << "GStreamer 초기화 실패";
        return false;
    }

    // 이미 다른 영상이 재생 중이라면 안전하게 정리 후 재구성.
    this->stop();

    // 영상 소스 유형에 따라 파이프라인 앞단(source element)을 결정한다.
    //
    //   1) /dev/video* (V4L2 디바이스): v4l2src 로 USB 웹캠 등 캡처
    //   2) rtsp://     (RTSP URL):      rtspsrc 로 네트워크 영상 수신
    //   3) 그 외       (파일 경로):      filesrc 로 로컬 파일 디코딩
    //
    // 라이브 소스(V4L2, RTSP)는 sync=false 로 설정해 실시간 프레임을
    // 지연 없이 처리하고, 파일 소스는 sync=true 로 원본 framerate 에 맞춘다.
    std::string source;
    bool is_live = false;

    if (filepath.compare(0, 5, "/dev/") == 0) {
        // V4L2 디바이스 (USB 웹캠 등)
        source = "v4l2src device=\"" + filepath + "\" ! decodebin";
        is_live = true;
    } else if (filepath.compare(0, 7, "rtsp://") == 0 ||
               filepath.compare(0, 8, "rtsps://") == 0) {
        // RTSP 스트림 수신
        source = "rtspsrc location=\"" + filepath + "\" latency=200 ! decodebin";
        is_live = true;
    } else {
        // 로컬 비디오 파일
        source = "filesrc location=\"" + filepath + "\" ! decodebin";
    }

    const std::string sync_val = is_live ? "false" : "true";
    const std::string desc =
        source + " ! videoconvert ! video/x-raw,format=RGB ! "
        "appsink name=sink emit-signals=false sync=" + sync_val +
        " max-buffers=1 drop=true";

    // desc 문자열을 파싱해 element 인스턴스화 + property 설정 + pad link까지
    // 한 번에 수행하고, 완성된 GstPipeline을 반환한다.
    GError* err = nullptr;
    this->pipeline_ = gst_parse_launch(desc.c_str(), &err);
    if (!this->pipeline_) {
        qWarning() << "GStreamer pipeline 생성 실패:"
                   << (err ? err->message : "unknown");
        if (err) g_error_free(err);
        return false;
    }

    // 파이프라인 안의 appsink 요소를 이름으로 룩업.
    this->appsink_ = gst_bin_get_by_name(GST_BIN(this->pipeline_), "sink");
    if (!this->appsink_) {
        qWarning() << "appsink 요소를 찾을 수 없음";
        this->stop();
        return false;
    }

    // appsink가 새 샘플을 받을 때마다 호출될 콜백 등록(emit-signals=false 모드).
    GstAppSinkCallbacks callbacks = {};
    callbacks.new_sample = &VideoPipeline::onNewSample;
    gst_app_sink_set_callbacks(GST_APP_SINK(this->appsink_), &callbacks, this, nullptr);

    // 파이프라인 상태를 PLAYING으로 전환 → 디코딩/콜백 시작.
    GstStateChangeReturn ret = gst_element_set_state(this->pipeline_, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        qWarning() << "파이프라인 PLAYING 전환 실패";
        this->stop();
        return false;
    }

    return true;
}

void VideoPipeline::stop() {
    // 다음 start() 에서 새 비디오의 fps 가 다시 잡히도록 리셋.
    this->fps_.store(0);

    // appsink는 gst_bin_get_by_name으로 ref가 늘어나 있으므로 unref로 해제.
    if (this->appsink_) {
        gst_object_unref(this->appsink_);
        this->appsink_ = nullptr;
    }
    if (this->pipeline_) {
        // PLAYING → NULL 전환으로 모든 element를 정지/언링크 후, 파이프라인 자체를 unref.
        gst_element_set_state(this->pipeline_, GST_STATE_NULL);
        gst_object_unref(this->pipeline_);
        this->pipeline_ = nullptr;
    }
}

GstFlowReturn VideoPipeline::onNewSample(GstAppSink* sink, gpointer user_data) {
    // static 멤버 함수이므로 this가 없음 → user_data로 전달된 인스턴스 포인터 사용
    auto* videoPipeline = static_cast<VideoPipeline*>(user_data);

    // appsink에서 디코딩된 한 프레임(샘플)을 꺼낸다(소유권 이전).
    GstSample* sample = gst_app_sink_pull_sample(sink);
    if (!sample) {
        return GST_FLOW_ERROR;
    }

    // caps에서 프레임 해상도(width, height)와 framerate 를 추출.
    GstCaps* caps = gst_sample_get_caps(sample);
    int width = 0, height = 0;
    if (caps) {
        GstStructure* s = gst_caps_get_structure(caps, 0);
        gst_structure_get_int(s, "width", &width);
        gst_structure_get_int(s, "height", &height);

        // framerate 는 fraction (num/den) 형식. fps = num / den 으로 환산.
        // 이미 값이 잡혀 있으면 매 프레임 갱신할 필요 없으므로 한 번만 저장.
        if (videoPipeline->fps_.load() == 0) {
            int fpsNum = 0, fpsDen = 0;
            if (gst_structure_get_fraction(s, "framerate", &fpsNum, &fpsDen) &&
                fpsDen > 0) {
                videoPipeline->fps_.store(fpsNum / fpsDen);
            }
        }
    }

    // 실제 픽셀 데이터를 가진 GstBuffer를 가져와 read 모드로 매핑.
    GstBuffer* buffer = gst_sample_get_buffer(sample);
    GstMapInfo map;
    if (buffer && width > 0 && height > 0 &&
        gst_buffer_map(buffer, &map, GST_MAP_READ)) {
        // QImage는 deep copy로 만들어 buffer 수명과 분리
        QImage frame(map.data, width, height,
                     static_cast<int>(map.size / height),
                     QImage::Format_RGB888);
        QImage copy = frame.copy();
        // 픽셀 복사가 끝났으므로 GStreamer 버퍼는 즉시 언맵.
        gst_buffer_unmap(buffer, &map);

        // 콜백은 GStreamer streaming thread에서 호출되므로
        // QueuedConnection으로 GUI 스레드에 전달됨 (수신 객체가 GUI 스레드 affinity인 경우)
        QMetaObject::invokeMethod(videoPipeline, "frameReady", Qt::QueuedConnection,
                                  Q_ARG(QImage, copy));
    }

    // pull_sample에서 받은 샘플 ref 해제.
    gst_sample_unref(sample);
    return GST_FLOW_OK;
}
