#include "Preprocessor.h"

cv::Mat Preprocessor::letterbox(const cv::Mat& src, int target_w, int target_h, LetterboxInfo& info) {
    // 가로/세로 비율 중 더 작은 쪽을 선택해 종횡비를 보존하면서 target 크기 안에 들어가게 한다.
    float scale = std::min(
        static_cast<float>(target_w) / src.cols,
        static_cast<float>(target_h) / src.rows);

    // 스케일 적용 후 실제 리사이즈될 크기.
    int new_w = static_cast<int>(src.cols * scale);
    int new_h = static_cast<int>(src.rows * scale);

    // 후처리에서 좌표를 원본으로 역변환할 때 필요한 스케일/패딩 정보를 저장.
    info.scale = scale;
    info.pad_x = (target_w - new_w) / 2;
    info.pad_y = (target_h - new_h) / 2;

    // 종횡비 유지한 채 리사이즈.
    cv::Mat resized;
    cv::resize(src, resized, cv::Size(new_w, new_h));

    // YOLO 표준 letterbox 회색(114) 배경 캔버스를 만들고, 리사이즈된 이미지를 중앙에 복사.
    cv::Mat padded(target_h, target_w, CV_8UC3, cv::Scalar(114, 114, 114));
    resized.copyTo(padded(cv::Rect(info.pad_x, info.pad_y, new_w, new_h)));
    return padded;
}
