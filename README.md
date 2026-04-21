# Hailo Edge Inference & Secure Streaming System

An on-device video inference and ML-KEM-based protected streaming system using the Hailo-8 AI accelerator.

On the edge device, a 640x640 anchor-based YOLO-family detection head is executed with HailoRT, and the video composited with inference results is transmitted to a remote client through a native RTSP/RTP stack. ML-KEM-derived ARIA keys are used to protect the control channel and RTP payload.

## Architecture

```
┌─────────────────────────────────────────────┐
│  Server  (Edge Device + Hailo-8)            │
│                                             │
│  Video Source ──► GStreamer Decode          │
│       (file / V4L2 / RTSP)                  │
│                    │                        │
│                    ▼                        │
│  Preprocessor (letterbox 640×640)           │
│                    │                        │
│                    ▼                        │
│  HailoInference (YOLO-style head on Hailo-8)│
│                    │                        │
│                    ▼                        │
│  PostProcessor (decode + NMS)               │
│                    │                        │
│                    ▼                        │
│  Visualizer (bbox overlay)                  │
│                    │                        │
│                    ▼                        │
│  H264Encoder ──► RtspServer                 │
│  (x264, GStreamer)   (custom RTSP/RTP +     │
│                     ML-KEM/ARIA protection) │
└──────────────────────┬──────────────────────┘
                       │ encrypted RTSP control
                       │ + protected RTP media
                       ▼
┌─────────────────────────────────────────────┐
│  Client  (Remote Monitoring)                │
│                                             │
│  RtspClient ──► NalDecoderPipeline ──► GUI  │
│  (ARIA/HMAC verify) (GStreamer H.264)  (Qt) │
└─────────────────────────────────────────────┘
```

## Key Features

### On-Device Inference

- **Model assumption**: A YOLOv5-style head using 640x640 input, 80 COCO classes, and 3 anchors/scales (80x80, 40x40, 20x20)
- **Inference engine**: HailoRT `VDevice` + HEF loading, executed one frame at a time through input/output VStreams
- **Preprocessing**: Letterbox resize (640x640) with aspect-ratio-preserving padding
- **Postprocessing**: Sigmoid activation, confidence filtering (threshold 0.25), and NMS (IoU 0.45)
- **Visualization**: Bounding boxes and class labels overlaid on the original resolution
- **Input sources**: Local files, V4L2 devices (`/dev/video*`), and RTSP URLs

### Post-Quantum Secure Streaming

| Component | Implementation | Notes |
|------|----------|------|
| Key exchange | **ML-KEM-768** (FIPS 203) | KEM based on the OpenSSL 3.5+ EVP API |
| Key derivation | **HKDF-SHA256** | Separately derives SRTP/RTSP keys from `shared_secret` and both sides' random values |
| RTP payload protection | **ARIA-128 CTR** | Keeps the RTP header intact and encrypts only the payload in place |
| RTP authentication | **HMAC-SHA1** (80-bit truncated) | Appends a 10-byte tag at the end of each packet |
| RTSP control channel protection | **ARIA-128 CTR** | Uses `[4-byte length][encrypted payload]` framing |

**Handshake Flow**:

```text
Client                                       Server
  |                                             |
  |---- TCP connect --------------------------->|
  |                                             |
  |<--- ServerHello ----------------------------|
  |     server_random                           |
  |     server public key                       |
  |                                             |
  |  make shared_secret locally                 |
  |  using server public key                    |
  |  make ml_kem_ciphertext locally             |
  |                                             |
  |---- ClientHello --------------------------->|
  |     client_random                           |
  |     ml_kem_ciphertext                       |
  |                                             |
  |                               make the same |
  |                               shared_secret |
  |                               locally using |
  |                         server private key  |
  |                                             |
  |<==== both sides run HKDF locally ========>  |
  |     input: shared_secret                    |
  |            client_random || server_random   |
  |     output: srtp_key, srtp_iv,              |
  |             srtp_auth_key,                  |
  |             rtsp_key, rtsp_iv               |
  |                                             |
  |===== encrypted RTSP / protected RTP ======= |
```

### RTSP/RTP Implementation

Instead of using the GStreamer RTSP library, this project uses a native RTSP server/client implemented directly on top of POSIX sockets.

- **RTSP signaling**: OPTIONS, DESCRIBE, SETUP, PLAY, TEARDOWN
- **Control channel handling**: After the handshake, RTSP requests/responses are encrypted and decrypted with per-session ARIA keys
- **RTP packetization**: RFC 6184 (H.264 over RTP)
- **Transport modes**: UDP (default) / TCP Interleaved

### Qt GUI + GStreamer Video Pipeline

- **GUI**: Based on Qt6 (Qt5-compatible), with separate windows for the server and client
- **Video decoding**: GStreamer (`filesrc`/`v4l2src`/`rtspsrc` -> `decodebin` -> `videoconvert` -> `appsink`)
- **H.264 encoding**: GStreamer (`appsrc(RGB)` -> `videoconvert` -> `x264enc` -> `h264parse` -> `appsink`), with output in access-unit units
- **NAL decoding**: GStreamer (`appsrc` -> `h264parse` -> `avdec_h264` -> `videoconvert` -> `appsink`)
- **Current status**: The server window is responsible for inference/streaming, but the local `QLabel` preview update code is currently disabled

## Project Structure

```
├── CMakeLists.txt                      # Build configuration
├── config.ini                          # Runtime configuration
├── models/                             # HEF model files
└── src/
    ├── main.cpp                        # Entry point
    ├── HailoInference.h/cpp            # HailoRT wrapper
    ├── Preprocessor.h/cpp              # Letterbox preprocessing
    ├── PostProcessor.h/cpp             # Anchor-based YOLO head decode + NMS
    ├── Visualizer.h/cpp                # Detection overlay
    ├── YoloTypes.h                     # Constants & data structures
    ├── gui/
    │   ├── GuiApp.h/cpp                # Qt bootstrap & role dispatch
    │   ├── server/
    │   │   ├── ServerWindow.h/cpp      # Server GUI (inference + streaming)
    │   │   └── ServerRole.cpp          # Server entry (weak/strong symbol)
    │   └── client/
    │       └── ClientWindow.h/cpp      # Client GUI (RTSP receive + display)
    ├── gstreamer/
    │   ├── GstBootstrap.h/cpp          # GStreamer environment setup
    │   ├── VideoPipeline.h/cpp         # Video source decode
    │   ├── H264Encoder.h/cpp           # BGR → H.264 encode
    │   └── NalDecoderPipeline.h/cpp    # H.264 NAL → RGB decode
    ├── rtsp_native/
    │   ├── RtspServer.h/cpp            # RTSP/RTP server
    │   ├── RtspClient.h/cpp            # RTSP/RTP client
    │   └── MlKemHandshake.h/cpp        # ML-KEM-based custom handshake
    └── crypto/
        ├── ICipher.h                   # Abstract cipher interface
        └── AriaCipher.h/cpp            # ARIA-128 CTR implementation
```

## Build

### Dependencies

| Library | Version Requirement | Purpose |
|------------|-------|------|
| Qt | 6.x (5.x fallback) | GUI, signals/slots |
| GStreamer | 1.0+  | Video decoding/encoding |
| OpenCV | 4.x   | Image processing and frame conversion |
| OpenSSL | **3.5+** | ML-KEM, HKDF, HMAC |
| HailoRT | 4.23  | Hailo-8 inference (server only) |

### Build Commands

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

Build outputs:

- `hailo_server` - inference + streaming server (requires HailoRT)
- `hailo_client` - remote monitoring client (does not require HailoRT)

> In environments where HailoRT is not installed, only `hailo_client` is built.

## Configuration

The `config.ini` file controls runtime behavior. In server mode, if `video_path` is empty, `resource/sample.mp4` in the parent directory of the executable is used as the default input.

```ini
[mode]
role = server              # server or client

[server]
hef_path = models/yolo_model.hef
video_path =               # if empty, use resource/sample.mp4

[rtsp]
port = 8554
stream_path = /stream
protocol = udp             # udp or tcp

[client]
server_ip = 127.0.0.1
```

## Usage

**Server (edge device)**:
```bash
./hailo_server config.ini
```

**Client (remote monitoring)**:
```bash
./hailo_client config.ini
```

After launching the client, you can either enter the server IP in the GUI or preconfigure `server_ip` in `config.ini` for automatic connection.
