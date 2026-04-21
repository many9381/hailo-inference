#pragma once

#include <vector>
#include <string>

// ── YOLOv5 model constants ───────────────────────────────────────────────────
static constexpr int INPUT_W = 640;
static constexpr int INPUT_H = 640;
static constexpr int NUM_CLASSES = 80;
static constexpr int NUM_ANCHORS = 3;
static constexpr int OUTPUTS_PER_ANCHOR = 5 + NUM_CLASSES; // 85

static constexpr float CONF_THRESHOLD = 0.25f;
static constexpr float NMS_IOU_THRESHOLD = 0.45f;

// Default YOLOv5 anchors (based on 640x640, in pixels)
static const float ANCHORS[3][3][2] = {
    {{10, 13},  {16, 30},   {33, 23}},   // P3 (80x80)
    {{30, 61},  {62, 45},   {59, 119}},  // P4 (40x40)
    {{116, 90}, {156, 198}, {373, 326}}, // P5 (20x20)
};

// COCO 80 class names
static const char* COCO_NAMES[] = {
    "person", "bicycle", "car", "motorcycle", "airplane", "bus", "train", "truck",
    "boat", "traffic light", "fire hydrant", "stop sign", "parking meter", "bench",
    "bird", "cat", "dog", "horse", "sheep", "cow", "elephant", "bear", "zebra",
    "giraffe", "backpack", "umbrella", "handbag", "tie", "suitcase", "frisbee",
    "skis", "snowboard", "sports ball", "kite", "baseball bat", "baseball glove",
    "skateboard", "surfboard", "tennis racket", "bottle", "wine glass", "cup",
    "fork", "knife", "spoon", "bowl", "banana", "apple", "sandwich", "orange",
    "broccoli", "carrot", "hot dog", "pizza", "donut", "cake", "chair", "couch",
    "potted plant", "bed", "dining table", "toilet", "tv", "laptop", "mouse",
    "remote", "keyboard", "cell phone", "microwave", "oven", "toaster", "sink",
    "refrigerator", "book", "clock", "vase", "scissors", "teddy bear",
    "hair drier", "toothbrush"
};

// ── Detection result struct ──────────────────────────────────────────────────
struct Detection {
    float x1, y1, x2, y2; // xyxy (normalized to 0-1)
    float confidence;
    int class_id;
};

// ── Letterbox information ────────────────────────────────────────────────────
struct LetterboxInfo {
    float scale;
    int pad_x, pad_y;
};
