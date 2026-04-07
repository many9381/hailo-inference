#pragma once

#include <vector>
#include <opencv2/opencv.hpp>
#include "YoloTypes.h"

class Visualizer {
public:
    static cv::Mat draw(const cv::Mat& original, const std::vector<Detection>& dets,
                        const LetterboxInfo& info);
};
