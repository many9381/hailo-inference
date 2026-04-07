#pragma once

#include <opencv2/opencv.hpp>
#include "YoloTypes.h"

class Preprocessor {
public:
    static cv::Mat letterbox(const cv::Mat& src, int target_w, int target_h, LetterboxInfo& info);
};
