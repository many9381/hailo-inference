#include "HailoInference.h"
#include <iostream>
#include <thread>
#include <stdexcept>

HailoInference::HailoInference(const std::string& hef_path) {
    std::cout << "HailoRT 초기화 중..." << std::endl;

    // VDevice 생성: Hailo 가속기에 대한 가상 디바이스 핸들. 추론 자원 컨테이너 역할.
    auto vdevice_exp = hailort::VDevice::create();
    if (!vdevice_exp) {
        throw std::runtime_error("VDevice 생성 실패: " + std::to_string(vdevice_exp.status()));
    }
    this->vdevice_ = vdevice_exp.release();

    // HEF(Hailo Executable Format) 로드: 컴파일된 모델 바이너리.
    auto hef_exp = hailort::Hef::create(hef_path);
    if (!hef_exp) {
        throw std::runtime_error("HEF 로드 실패: " + hef_path);
    }
    auto hef = hef_exp.release();

    // HEF를 디바이스에 매핑해 네트워크 그룹(추론 가능한 그래프)으로 만든다.
    auto net_groups_exp = this->vdevice_->configure(hef);
    if (!net_groups_exp) {
        throw std::runtime_error("네트워크 구성 실패: " + std::to_string(net_groups_exp.status()));
    }
    auto& net_groups = net_groups_exp.value();
    // 본 모델은 단일 네트워크 그룹만 가지므로 첫 번째 항목을 사용.
    this->net_group_ = net_groups[0];

    // VStream 파라미터 설정.
    // 입력은 양자화된 UINT8 텐서로 직접 푸시(별도 quantization 단계 생략).
    auto input_params = this->net_group_->make_input_vstream_params(
        true, HAILO_FORMAT_TYPE_UINT8, HAILO_DEFAULT_VSTREAM_TIMEOUT_MS,
        HAILO_DEFAULT_VSTREAM_QUEUE_SIZE);
    if (!input_params) {
        throw std::runtime_error("입력 VStream 파라미터 생성 실패");
    }

    // 출력은 FP32로 dequantize 된 결과를 받는다(후처리 편의를 위해).
    auto output_params = this->net_group_->make_output_vstream_params(
        true, HAILO_FORMAT_TYPE_FLOAT32, HAILO_DEFAULT_VSTREAM_TIMEOUT_MS,
        HAILO_DEFAULT_VSTREAM_QUEUE_SIZE);
    if (!output_params) {
        throw std::runtime_error("출력 VStream 파라미터 생성 실패");
    }

    // 실제 입출력 VStream 객체 인스턴스화 — 여기서부터 read/write 가능.
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
    // HailoRT VStream은 write/read가 모두 블로킹이므로 같은 스레드에서 순차 호출하면 데드락이 난다.
    // → write는 별도 스레드에 위임하고, 메인 스레드에서는 출력 read를 진행한다.
    hailo_status write_status = HAILO_UNINITIALIZED;
    std::thread write_thread([&]() {
        // cv::Mat 픽셀 버퍼를 그대로 입력 VStream에 푸시(전처리는 호출자 책임).
        write_status = this->input_vstreams_[0].write(
            hailort::MemoryView(input.data, input.total() * input.elemSize()));
    });

    // 출력 VStream을 순회하면서 각 텐서의 raw float 버퍼를 수신.
    std::vector<std::pair<std::string, std::vector<float>>> outputs;
    for (auto& vs : this->output_vstreams_) {
        // VStream이 알려주는 한 프레임 분량 크기에 맞춰 버퍼 할당.
        size_t frame_size = vs.get_frame_size();
        size_t num_floats = frame_size / sizeof(float);
        std::vector<float> buffer(num_floats);
        auto status = vs.read(hailort::MemoryView(buffer.data(), frame_size));
        if (status != HAILO_SUCCESS) {
            // 실패 시에도 write 스레드가 매달려있지 않도록 먼저 join 후 예외.
            write_thread.join();
            throw std::runtime_error("출력 읽기 실패: " + std::string(vs.name()));
        }
        // (스트림 이름, 버퍼) 페어로 보관 — 후처리에서 텐서 식별에 사용.
        outputs.emplace_back(vs.name(), std::move(buffer));
    }

    // 모든 출력 수신 후 write 스레드 정리.
    write_thread.join();
    if (write_status != HAILO_SUCCESS) {
        throw std::runtime_error("입력 쓰기 실패");
    }

    return outputs;
}
