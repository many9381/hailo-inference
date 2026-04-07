#include "HailoInference.h"
#include <iostream>
#include <thread>
#include <stdexcept>

HailoInference::HailoInference(const std::string& hef_path) {
    std::cout << "HailoRT 초기화 중..." << std::endl;

    // VDevice 생성
    auto vdevice_exp = hailort::VDevice::create();
    if (!vdevice_exp) {
        throw std::runtime_error("VDevice 생성 실패: " + std::to_string(vdevice_exp.status()));
    }
    this->vdevice_ = vdevice_exp.release();

    // HEF 로드
    auto hef_exp = hailort::Hef::create(hef_path);
    if (!hef_exp) {
        throw std::runtime_error("HEF 로드 실패: " + hef_path);
    }
    auto hef = hef_exp.release();

    // 네트워크 그룹 구성
    auto net_groups_exp = this->vdevice_->configure(hef);
    if (!net_groups_exp) {
        throw std::runtime_error("네트워크 구성 실패: " + std::to_string(net_groups_exp.status()));
    }
    auto& net_groups = net_groups_exp.value();
    this->net_group_ = net_groups[0];

    // VStream 파라미터 설정
    auto input_params = this->net_group_->make_input_vstream_params(
        true, HAILO_FORMAT_TYPE_UINT8, HAILO_DEFAULT_VSTREAM_TIMEOUT_MS,
        HAILO_DEFAULT_VSTREAM_QUEUE_SIZE);
    if (!input_params) {
        throw std::runtime_error("입력 VStream 파라미터 생성 실패");
    }

    auto output_params = this->net_group_->make_output_vstream_params(
        true, HAILO_FORMAT_TYPE_FLOAT32, HAILO_DEFAULT_VSTREAM_TIMEOUT_MS,
        HAILO_DEFAULT_VSTREAM_QUEUE_SIZE);
    if (!output_params) {
        throw std::runtime_error("출력 VStream 파라미터 생성 실패");
    }

    // VStream 생성
    auto input_vstreams_exp = this->net_group_->create_input_vstreams(input_params.value());
    if (!input_vstreams_exp) {
        throw std::runtime_error("입력 VStream 생성 실패");
    }
    this->input_vstreams_ = input_vstreams_exp.release();

    auto output_vstreams_exp = this->net_group_->create_output_vstreams(output_params.value());
    if (!output_vstreams_exp) {
        throw std::runtime_error("출력 VStream 생성 실패");
    }
    this->output_vstreams_ = output_vstreams_exp.release();

    // 스트림 정보 표시
    std::cout << "입력 스트림: " << this->input_vstreams_.size() << "개" << std::endl;
    for (auto& vs : this->input_vstreams_) {
        std::cout << "  - " << vs.name() << " (frame_size: " << vs.get_frame_size() << ")" << std::endl;
    }
    std::cout << "출력 스트림: " << this->output_vstreams_.size() << "개" << std::endl;
    for (auto& vs : this->output_vstreams_) {
        std::cout << "  - " << vs.name() << " (frame_size: " << vs.get_frame_size() << ")" << std::endl;
    }
}

std::vector<std::pair<std::string, std::vector<float>>> HailoInference::run(const cv::Mat& input) {
    // 입력 전송 (별도 스레드에서 write, 메인 스레드에서 read)
    hailo_status write_status = HAILO_UNINITIALIZED;
    std::thread write_thread([&]() {
        write_status = this->input_vstreams_[0].write(
            hailort::MemoryView(input.data, input.total() * input.elemSize()));
    });

    // 출력 수신
    std::vector<std::pair<std::string, std::vector<float>>> outputs;
    for (auto& vs : this->output_vstreams_) {
        size_t frame_size = vs.get_frame_size();
        size_t num_floats = frame_size / sizeof(float);
        std::vector<float> buffer(num_floats);
        auto status = vs.read(hailort::MemoryView(buffer.data(), frame_size));
        if (status != HAILO_SUCCESS) {
            write_thread.join();
            throw std::runtime_error("출력 읽기 실패: " + std::string(vs.name()));
        }
        outputs.emplace_back(vs.name(), std::move(buffer));
    }

    write_thread.join();
    if (write_status != HAILO_SUCCESS) {
        throw std::runtime_error("입력 쓰기 실패");
    }

    return outputs;
}
