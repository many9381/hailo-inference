#include "Preprocessor.h"

cv::Mat Preprocessor::letterbox(const cv::Mat& src, int target_w, int target_h, LetterboxInfo& info) {
    // Choose the smaller width/height ratio so the image fits within the target while preserving aspect ratio.
    float scale = std::min(
        static_cast<float>(target_w) / src.cols,
        static_cast<float>(target_h) / src.rows);

    // Actual resized dimensions after applying the scale.
    int new_w = static_cast<int>(src.cols * scale);
    int new_h = static_cast<int>(src.rows * scale);

    // Store the scale/padding information needed to map coordinates back to the original image in postprocessing.
    info.scale = scale;
    info.pad_x = (target_w - new_w) / 2;
    info.pad_y = (target_h - new_h) / 2;

    // Resize while preserving aspect ratio.
    cv::Mat resized;
    cv::resize(src, resized, cv::Size(new_w, new_h));

    // Create the standard YOLO gray (114) letterbox background canvas and copy the resized image into the center.
    cv::Mat padded(target_h, target_w, CV_8UC3, cv::Scalar(114, 114, 114));
    resized.copyTo(padded(cv::Rect(info.pad_x, info.pad_y, new_w, new_h)));
    return padded;
}
