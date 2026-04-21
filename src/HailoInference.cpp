#include "HailoInference.h"
#include <iostream>
#include <thread>
#include <stdexcept>

HailoInference::HailoInference(const std::string& hef_path) {
    std::cout << "Initializing HailoRT..." << std::endl;

    // Create the VDevice: a virtual device handle for the Hailo accelerator that acts as a container for inference resources.
    auto vdevice_exp = hailort::VDevice::create();
    if (!vdevice_exp) {
        throw std::runtime_error("Failed to create VDevice: " + std::to_string(vdevice_exp.status()));
    }
    this->vdevice_ = vdevice_exp.release();

    // Load the HEF (Hailo Executable Format): the compiled model binary.
    auto hef_exp = hailort::Hef::create(hef_path);
    if (!hef_exp) {
        throw std::runtime_error("Failed to load HEF: " + hef_path);
    }
    auto hef = hef_exp.release();

    // Map the HEF onto the device to create a network group (an inferable graph).
    auto net_groups_exp = this->vdevice_->configure(hef);
    if (!net_groups_exp) {
        throw std::runtime_error("Failed to configure network: " + std::to_string(net_groups_exp.status()));
    }
    auto& net_groups = net_groups_exp.value();
    // This model has only a single network group, so use the first entry.
    this->net_group_ = net_groups[0];

    // Configure the VStream parameters.
    // The input is pushed directly as a quantized UINT8 tensor (skipping a separate quantization step).
    auto input_params = this->net_group_->make_input_vstream_params(
        true, HAILO_FORMAT_TYPE_UINT8, HAILO_DEFAULT_VSTREAM_TIMEOUT_MS,
        HAILO_DEFAULT_VSTREAM_QUEUE_SIZE);
    if (!input_params) {
        throw std::runtime_error("Failed to create input VStream parameters");
    }

    // The output is received as FP32-dequantized data for easier postprocessing.
    auto output_params = this->net_group_->make_output_vstream_params(
        true, HAILO_FORMAT_TYPE_FLOAT32, HAILO_DEFAULT_VSTREAM_TIMEOUT_MS,
        HAILO_DEFAULT_VSTREAM_QUEUE_SIZE);
    if (!output_params) {
        throw std::runtime_error("Failed to create output VStream parameters");
    }

    // Instantiate the actual input/output VStream objects. Read/write becomes available from this point.
    auto input_vstreams_exp = this->net_group_->create_input_vstreams(input_params.value());
    if (!input_vstreams_exp) {
        throw std::runtime_error("Failed to create input VStream");
    }
    this->input_vstreams_ = input_vstreams_exp.release();

    auto output_vstreams_exp = this->net_group_->create_output_vstreams(output_params.value());
    if (!output_vstreams_exp) {
        throw std::runtime_error("Failed to create output VStream");
    }
    this->output_vstreams_ = output_vstreams_exp.release();

    // Print stream information
    std::cout << "Input streams: " << this->input_vstreams_.size() << std::endl;
    for (auto& vs : this->input_vstreams_) {
        std::cout << "  - " << vs.name() << " (frame_size: " << vs.get_frame_size() << ")" << std::endl;
    }
    std::cout << "Output streams: " << this->output_vstreams_.size() << std::endl;
    for (auto& vs : this->output_vstreams_) {
        std::cout << "  - " << vs.name() << " (frame_size: " << vs.get_frame_size() << ")" << std::endl;
    }
}

std::vector<std::pair<std::string, std::vector<float>>> HailoInference::run(const cv::Mat& input) {
    // HailoRT VStream write/read operations are both blocking, so sequential calls on the same thread would deadlock.
    // -> Delegate write to a separate thread, and perform output reads on the main thread.
    hailo_status write_status = HAILO_UNINITIALIZED;
    std::thread write_thread([&]() {
        // Push the cv::Mat pixel buffer directly into the input VStream (preprocessing is the caller's responsibility).
        write_status = this->input_vstreams_[0].write(
            hailort::MemoryView(input.data, input.total() * input.elemSize()));
    });

    // Iterate over the output VStreams and receive each tensor's raw float buffer.
    std::vector<std::pair<std::string, std::vector<float>>> outputs;
    for (auto& vs : this->output_vstreams_) {
        // Allocate a buffer sized to one frame as reported by the VStream.
        size_t frame_size = vs.get_frame_size();
        size_t num_floats = frame_size / sizeof(float);
        std::vector<float> buffer(num_floats);
        auto status = vs.read(hailort::MemoryView(buffer.data(), frame_size));
        if (status != HAILO_SUCCESS) {
            // Join the write thread before throwing so it does not remain stuck even on failure.
            write_thread.join();
            throw std::runtime_error("Failed to read output: " + std::string(vs.name()));
        }
        // Store the data as a (stream name, buffer) pair for tensor identification in postprocessing.
        outputs.emplace_back(vs.name(), std::move(buffer));
    }

    // Clean up the write thread after receiving all outputs.
    write_thread.join();
    if (write_status != HAILO_SUCCESS) {
        throw std::runtime_error("Failed to write input");
    }

    return outputs;
}
