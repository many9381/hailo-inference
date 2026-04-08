#include "VideoPipeline.h"

#include <QDebug>

VideoPipeline::VideoPipeline(QObject* parent) : QObject(parent) {
    // GStreamer는 프로세스 전역에 한 번만 초기화되어야 한다(중복 호출 방지).
    if (!gst_is_initialized()) {
        gst_init(nullptr, nullptr);
    }
}

VideoPipeline::~VideoPipeline() {
    this->stop();
}

bool VideoPipeline::start(const std::string& filepath) {
    // 이미 다른 영상이 재생 중이라면 안전하게 정리 후 재구성.
    this->stop();

    // 파이프라인 정의: 파일을 디코드해 RGB 프레임을 appsink 큐에 1장만 유지(드롭 정책).
    // max-buffers=1, drop=true로 추론이 느릴 때 latency를 낮춘다.
    //
    // 데이터 흐름 (element 그래프):
    //
    //   filesrc          -> decodebin       -> videoconvert    -> [caps: RGB]    -> appsink
    //   (파일 바이트 읽기)   (컨테이너/코덱 디코딩)   (픽셀 포맷 변환)      (RGB 강제 필터)      (앱으로 전달)
    //
    //   * filesrc      : location 파일을 열어 raw 바이트 스트림 출력
    //   * decodebin    : MP4/H.264 등 자동 감지 후 decoded video frame으로 변환
    //   * videoconvert : 디코더 출력 포맷(YUV 등)을 다른 포맷으로 변환 가능하게 어댑팅
    //   * caps filter  : 다음 단으로 흘릴 데이터 형식을 RGB로 강제(이 지점에서 포맷 확정)
    //   * appsink      : 파이프라인 끝단. 우리 콜백(onNewSample)으로 프레임을 꺼내감
    const std::string desc =
        "filesrc location=\"" + filepath + "\" ! "
        "decodebin ! videoconvert ! video/x-raw,format=RGB ! "
        "appsink name=sink emit-signals=false sync=true max-buffers=1 drop=true";

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

    // caps에서 프레임 해상도(width, height)를 추출.
    GstCaps* caps = gst_sample_get_caps(sample);
    int width = 0, height = 0;
    if (caps) {
        GstStructure* s = gst_caps_get_structure(caps, 0);
        gst_structure_get_int(s, "width", &width);
        gst_structure_get_int(s, "height", &height);
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
