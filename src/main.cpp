#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <filesystem>

#include <opencv2/opencv.hpp>

#include "YoloTypes.h"
#include "Preprocessor.h"
#include "PostProcessor.h"
#include "Visualizer.h"
#include "HailoInference.h"
#include "gui/GuiApp.h"

namespace fs = std::filesystem;

int hailo_inference(const std::string& hef_path, const std::string& images_dir, const std::string& output_dir) {
    fs::create_directories(output_dir);

    // 이미지 목록 수집
    std::vector<std::string> image_paths;
    for (auto& entry : fs::directory_iterator(images_dir)) {
        auto ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext == ".jpg" || ext == ".jpeg" || ext == ".png" || ext == ".bmp") {
            image_paths.push_back(entry.path().string());
        }
    }
    std::sort(image_paths.begin(), image_paths.end());

    if (image_paths.empty()) {
        std::cerr << "이미지를 찾을 수 없습니다: " << images_dir << std::endl;
        return 1;
    }
    std::cout << "이미지 " << image_paths.size() << "장 발견" << std::endl;

    // HailoRT 초기화
    HailoInference inference(hef_path);

    // 추론 루프
    std::cout << "\n추론 시작..." << std::endl;

    for (size_t idx = 0; idx < image_paths.size(); idx++) {
        auto& img_path = image_paths[idx];
        std::string filename = fs::path(img_path).filename().string();

        // 이미지 로드
        cv::Mat img = cv::imread(img_path);
        if (img.empty()) {
            std::cerr << "이미지 로드 실패: " << img_path << std::endl;
            continue;
        }

        // 전처리: letterbox + BGR→RGB
        LetterboxInfo lb_info;
        cv::Mat input_img = Preprocessor::letterbox(img, INPUT_W, INPUT_H, lb_info);
        cv::cvtColor(input_img, input_img, cv::COLOR_BGR2RGB);

        if (!input_img.isContinuous()) {
            input_img = input_img.clone();
        }

        // 추론
        auto outputs = inference.run(input_img);

        // 후처리: 디코딩 + NMS
        auto detections = PostProcessor::decode(outputs, CONF_THRESHOLD);

        // 결과 시각화 및 저장
        cv::Mat result = Visualizer::draw(img, detections, lb_info);
        std::string out_path = (fs::path(output_dir) / filename).string();
        cv::imwrite(out_path, result);

        std::cout << "[" << (idx + 1) << "/" << image_paths.size() << "] "
                  << filename << " → " << detections.size() << "개 검출" << std::endl;
    }

    std::cout << "\n완료! 결과: " << output_dir << std::endl;

    return 0;
}

int main(int argc, char* argv[]) {
    // 필수 인자: hef_path, images_dir, output_dir
    if (argc < 4) {
        std::cerr << "사용법: " << (argc > 0 ? argv[0] : "hailo_yolo_inference")
                  << " <hef_path> <images_dir> <output_dir> [video_path]" << std::endl;
        return 1;
    }

    [[maybe_unused]] std::string hef_path = argv[1];
    [[maybe_unused]] std::string images_dir = argv[2];
    [[maybe_unused]] std::string output_dir = argv[3];

    // video_path는 선택 인자 (GUI 재생용). 미지정 시 project_root 기준 기본값 사용.
    std::string video_path;
    if (argc >= 5) {
        video_path = argv[4];
    } else {
        fs::path exe_dir = fs::path(argv[0]).parent_path();
        fs::path project_root = exe_dir.parent_path();
        video_path = (project_root / "resource" / "VIRAT_S_000001.mp4").string();
    }

    return GuiApp::run(argc, argv, video_path);
}
