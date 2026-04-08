#include "PostProcessor.h"
#include <cmath>
#include <algorithm>

float PostProcessor::sigmoid(float x) {
    // 로짓을 [0, 1] 확률로 매핑. YOLO objectness/class score 디코딩에 사용.
    return 1.0f / (1.0f + std::exp(-x));
}

float PostProcessor::iou(const Detection& a, const Detection& b) {
    // 두 박스의 교집합 영역(좌상단은 max, 우하단은 min)을 구한다.
    float ix1 = std::max(a.x1, b.x1);
    float iy1 = std::max(a.y1, b.y1);
    float ix2 = std::min(a.x2, b.x2);
    float iy2 = std::min(a.y2, b.y2);
    // 음수가 나오면 교집합이 없는 경우이므로 0으로 클램프.
    float inter = std::max(0.0f, ix2 - ix1) * std::max(0.0f, iy2 - iy1);
    float area_a = (a.x2 - a.x1) * (a.y2 - a.y1);
    float area_b = (b.x2 - b.x1) * (b.y2 - b.y1);
    // IoU = 교집합 / 합집합. 분모 0 방지를 위해 epsilon 추가.
    return inter / (area_a + area_b - inter + 1e-6f);
}

std::vector<Detection> PostProcessor::nms(std::vector<Detection>& dets, float iou_thresh) {
    // 신뢰도 내림차순 정렬: 가장 강한 박스부터 채택하기 위함.
    std::sort(dets.begin(), dets.end(), [](const Detection& a, const Detection& b) {
        return a.confidence > b.confidence;
    });

    std::vector<Detection> result;
    // 억제(중복 제거) 여부를 인덱스 단위로 추적.
    std::vector<bool> suppressed(dets.size(), false);

    for (size_t i = 0; i < dets.size(); i++) {
        // 이미 억제된 박스는 건너뜀.
        if (suppressed[i]) continue;
        // 신뢰도가 가장 높은 박스를 결과에 채택.
        result.push_back(dets[i]);
        // 같은 클래스이면서 IoU가 임계치 이상으로 겹치는 후속 박스는 제거.
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
    // 각 출력 텐서에 대해 grid 크기와 anchor 인덱스를 추출한다.
    for (auto& [name, buf] : outputs) {
        // YOLOv5: 채널 = 3 anchors * (5 + 80 classes) = 255. hw = 셀 수.
        int total = static_cast<int>(buf.size());
        int hw = total / 255;
        // 정사각 grid 가정: 80x80, 40x40, 20x20.
        int grid = static_cast<int>(std::sqrt(hw));

        // grid 크기에 따라 사용할 anchor 세트 매핑(스케일별 anchor 다름).
        int anchor_idx;
        if (grid == 80) anchor_idx = 0;
        else if (grid == 40) anchor_idx = 1;
        else anchor_idx = 2;

        scales.push_back({grid, grid, anchor_idx, buf.data()});
    }

    std::vector<Detection> detections;

    // 모든 (scale, y, x, anchor) 조합을 순회하며 후보 박스를 디코딩.
    for (auto& s : scales) {
        for (int y = 0; y < s.grid_h; y++) {
            for (int x = 0; x < s.grid_w; x++) {
                for (int a = 0; a < NUM_ANCHORS; a++) {
                    // 현재 셀/anchor의 출력 시작 포인터. 메모리 레이아웃: [y][x][anchor][5+class].
                    const float* ptr = s.data +
                        (y * s.grid_w + x) * (NUM_ANCHORS * OUTPUTS_PER_ANCHOR) +
                        a * OUTPUTS_PER_ANCHOR;

                    // objectness(객체 존재 확률) 계산. 임계치 미만이면 일찍 컷.
                    float obj = sigmoid(ptr[4]);
                    if (obj < conf_thresh) continue;

                    // 모든 클래스 점수 중 최대값과 그 인덱스를 찾는다(argmax).
                    int best_cls = 0;
                    float best_score = -1.0f;
                    for (int c = 0; c < NUM_CLASSES; c++) {
                        float score = sigmoid(ptr[5 + c]);
                        if (score > best_score) {
                            best_score = score;
                            best_cls = c;
                        }
                    }

                    // 최종 신뢰도 = objectness * class score. 다시 한 번 임계치 필터.
                    float confidence = obj * best_score;
                    if (confidence < conf_thresh) continue;

                    // YOLOv5 박스 디코딩 공식:
                    // 중심 좌표는 셀 오프셋(sigmoid*2-0.5)에 셀 인덱스를 더해 grid 정규화.
                    float cx = (sigmoid(ptr[0]) * 2.0f - 0.5f + x) / s.grid_w;
                    float cy = (sigmoid(ptr[1]) * 2.0f - 0.5f + y) / s.grid_h;
                    // 너비/높이는 (sigmoid*2)^2 * anchor 크기를 입력 해상도로 정규화.
                    float w = std::pow(sigmoid(ptr[2]) * 2.0f, 2) *
                              ANCHORS[s.anchor_idx][a][0] / INPUT_W;
                    float h = std::pow(sigmoid(ptr[3]) * 2.0f, 2) *
                              ANCHORS[s.anchor_idx][a][1] / INPUT_H;

                    // (cx, cy, w, h) → (x1, y1, x2, y2) 코너 좌표로 변환.
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

    // 중복 박스 제거 후 최종 결과 반환.
    return nms(detections, NMS_IOU_THRESHOLD);
}
