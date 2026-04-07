#include "Visualizer.h"
#include <string>
#include <algorithm>

cv::Mat Visualizer::draw(const cv::Mat& original, const std::vector<Detection>& dets,
                         const LetterboxInfo& info) {
    cv::Mat img = original.clone();
    int orig_h = img.rows;
    int orig_w = img.cols;

    for (auto& d : dets) {
        // 정규화 좌표 → letterbox 픽셀 좌표 → 원본 좌표
        float x1 = d.x1 * INPUT_W;
        float y1 = d.y1 * INPUT_H;
        float x2 = d.x2 * INPUT_W;
        float y2 = d.y2 * INPUT_H;

        // letterbox 패딩 제거 후 원본 스케일로 변환
        x1 = (x1 - info.pad_x) / info.scale;
        y1 = (y1 - info.pad_y) / info.scale;
        x2 = (x2 - info.pad_x) / info.scale;
        y2 = (y2 - info.pad_y) / info.scale;

        // 클램핑
        x1 = std::clamp(x1, 0.0f, static_cast<float>(orig_w));
        y1 = std::clamp(y1, 0.0f, static_cast<float>(orig_h));
        x2 = std::clamp(x2, 0.0f, static_cast<float>(orig_w));
        y2 = std::clamp(y2, 0.0f, static_cast<float>(orig_h));

        // 색상 (클래스별로 다른 색)
        int color_idx = d.class_id % 20;
        cv::Scalar color(
            (color_idx * 37 + 50) % 256,
            (color_idx * 73 + 100) % 256,
            (color_idx * 113 + 150) % 256);

        cv::rectangle(img, cv::Point(x1, y1), cv::Point(x2, y2), color, 2);

        // 라벨
        std::string label = std::string(COCO_NAMES[d.class_id]) +
                            " " + std::to_string(static_cast<int>(d.confidence * 100)) + "%";
        int baseline = 0;
        cv::Size text_size = cv::getTextSize(label, cv::FONT_HERSHEY_SIMPLEX, 0.5, 1, &baseline);
        cv::rectangle(img,
                      cv::Point(x1, y1 - text_size.height - 4),
                      cv::Point(x1 + text_size.width, y1),
                      color, -1);
        cv::putText(img, label, cv::Point(x1, y1 - 2),
                    cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(255, 255, 255), 1);
    }
    return img;
}
