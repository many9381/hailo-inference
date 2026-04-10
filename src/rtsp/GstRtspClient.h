#pragma once

// Qt 의 signals 매크로와 GLib/GIO 의 "signals" 멤버 이름이 충돌하므로
// GLib 타입들을 전방 선언(forward declaration)만 하고 실제 include 는 .cpp 에서만 한다.

// GLib / GStreamer 전방 선언
typedef struct _GstElement  GstElement;
typedef struct _GstAppSink  GstAppSink;
typedef struct _GstPad      GstPad;

#include <QByteArray>
#include <QObject>

#include <atomic>
#include <string>

// ----------------------------------------------------------------------------
// GstRtspClient
//
// GStreamer rtspsrc 기반 RTSP/RTP H.264 클라이언트.
//
// 내부 파이프라인:
//   rtspsrc(동적 패드) → rtph264depay → h264parse → capsfilter(nal) → appsink
//
// appsink 에서 꺼낸 각 NAL 단위(Annex-B start code 미포함)를 nalReceived 시그널로
// 전달한다. 디코딩/표시는 이 클래스의 책임이 아니다.
//
// rtspsrc 는 RTSP 시그널링과 RTP 수신을 내부 스레드에서 처리하므로
// 별도의 GLib 메인 루프가 필요하지 않다.
// ----------------------------------------------------------------------------
class GstRtspClient : public QObject {
    Q_OBJECT

public:
    explicit GstRtspClient(QObject* parent = nullptr);
    ~GstRtspClient() override;

    GstRtspClient(const GstRtspClient&) = delete;
    GstRtspClient& operator=(const GstRtspClient&) = delete;

    // url: "rtsp://host:port/path" 형식.
    bool start(const std::string& url);
    void stop();

signals:
    // Annex-B start code 없는 raw NAL 한 단위가 도착할 때마다 emit.
    // [NAL header(1B) | RBSP ...] 형태.
    void nalReceived(const QByteArray& nal);

private:
    // rtspsrc 동적 패드가 준비될 때 rtph264depay 의 싱크 패드와 연결한다.
    static void onPadAdded(GstElement* src, GstPad* pad, void* userData);

    // appsink 에 새 샘플이 도착할 때마다 호출되는 콜백.
    // 반환 타입은 GstFlowReturn(int) 와 같으므로 int 로 선언하고 .cpp 에서 캐스팅한다.
    static int onNewSample(GstAppSink* sink, void* userData);

    GstElement*       pipeline_ = nullptr;
    std::atomic<bool> running_  = false;
};
