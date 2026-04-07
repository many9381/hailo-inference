#pragma once

#include <vector>
#include <string>
#include <memory>
#include <opencv2/opencv.hpp>
#include <hailo/hailort.hpp>

class HailoInference {
public:
    explicit HailoInference(const std::string& hef_path);

    std::vector<std::pair<std::string, std::vector<float>>> run(const cv::Mat& input);

private:
    std::unique_ptr<hailort::VDevice> vdevice_;
    std::shared_ptr<hailort::ConfiguredNetworkGroup> net_group_;
    std::vector<hailort::InputVStream> input_vstreams_;
    std::vector<hailort::OutputVStream> output_vstreams_;
};
