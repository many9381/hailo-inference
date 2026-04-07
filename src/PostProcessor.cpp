#include "PostProcessor.h"
#include <cmath>
#include <algorithm>

float PostProcessor::sigmoid(float x) {
    return 1.0f / (1.0f + std::exp(-x));
}

float PostProcessor::iou(const Detection& a, const Detection& b) {
    float ix1 = std::max(a.x1, b.x1);
    float iy1 = std::max(a.y1, b.y1);
    float ix2 = std::min(a.x2, b.x2);
    float iy2 = std::min(a.y2, b.y2);
    float inter = std::max(0.0f, ix2 - ix1) * std::max(0.0f, iy2 - iy1);
    float area_a = (a.x2 - a.x1) * (a.y2 - a.y1);
    float area_b = (b.x2 - b.x1) * (b.y2 - b.y1);
    return inter / (area_a + area_b - inter + 1e-6f);
}

std::vector<Detection> PostProcessor::nms(std::vector<Detection>& dets, float iou_thresh) {
    std::sort(dets.begin(), dets.end(), [](const Detection& a, const Detection& b) {
        return a.confidence > b.confidence;
    });

    std::vector<Detection> result;
    std::vector<bool> suppressed(dets.size(), false);

    for (size_t i = 0; i < dets.size(); i++) {
        if (suppressed[i]) continue;
        result.push_back(dets[i]);
        for (size_t j = i + 1; j < dets.size(); j++) {
            if (!suppressed[j] && dets[i].class_id == dets[j].class_id &&
                iou(dets[i], dets[j]) > iou_thresh) {
                suppressed[j] = true;
            }
        }
    }
    return result;
}

std::vector<Detection> PostProcessor::decode(
    const std::vector<std::pair<std::string, std::vector<float>>>& outputs,
    float conf_thresh)
{
    struct ScaleInfo {
        int grid_h, grid_w;
        int anchor_idx;
        const float* data;
    };

    std::vector<ScaleInfo> scales;
    for (auto& [name, buf] : outputs) {
        int total = static_cast<int>(buf.size());
        int hw = total / 255;
        int grid = static_cast<int>(std::sqrt(hw));

        int anchor_idx;
        if (grid == 80) anchor_idx = 0;
        else if (grid == 40) anchor_idx = 1;
        else anchor_idx = 2;

        scales.push_back({grid, grid, anchor_idx, buf.data()});
    }

    std::vector<Detection> detections;

    for (auto& s : scales) {
        for (int y = 0; y < s.grid_h; y++) {
            for (int x = 0; x < s.grid_w; x++) {
                for (int a = 0; a < NUM_ANCHORS; a++) {
                    const float* ptr = s.data +
                        (y * s.grid_w + x) * (NUM_ANCHORS * OUTPUTS_PER_ANCHOR) +
                        a * OUTPUTS_PER_ANCHOR;

                    float obj = sigmoid(ptr[4]);
                    if (obj < conf_thresh) continue;

                    int best_cls = 0;
                    float best_score = -1.0f;
                    for (int c = 0; c < NUM_CLASSES; c++) {
                        float score = sigmoid(ptr[5 + c]);
                        if (score > best_score) {
                            best_score = score;
                            best_cls = c;
                        }
                    }

                    float confidence = obj * best_score;
                    if (confidence < conf_thresh) continue;

                    float cx = (sigmoid(ptr[0]) * 2.0f - 0.5f + x) / s.grid_w;
                    float cy = (sigmoid(ptr[1]) * 2.0f - 0.5f + y) / s.grid_h;
                    float w = std::pow(sigmoid(ptr[2]) * 2.0f, 2) *
                              ANCHORS[s.anchor_idx][a][0] / INPUT_W;
                    float h = std::pow(sigmoid(ptr[3]) * 2.0f, 2) *
                              ANCHORS[s.anchor_idx][a][1] / INPUT_H;

                    Detection det;
                    det.x1 = cx - w / 2.0f;
                    det.y1 = cy - h / 2.0f;
                    det.x2 = cx + w / 2.0f;
                    det.y2 = cy + h / 2.0f;
                    det.confidence = confidence;
                    det.class_id = best_cls;
                    detections.push_back(det);
                }
            }
        }
    }

    return nms(detections, NMS_IOU_THRESHOLD);
}
