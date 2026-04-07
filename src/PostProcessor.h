#pragma once

#include <vector>
#include <string>
#include "YoloTypes.h"

class PostProcessor {
public:
    static std::vector<Detection> decode(
        const std::vector<std::pair<std::string, std::vector<float>>>& outputs,
        float conf_thresh);

private:
    static inline float sigmoid(float x);
    static float iou(const Detection& a, const Detection& b);
    static std::vector<Detection> nms(std::vector<Detection>& dets, float iou_thresh);
};
