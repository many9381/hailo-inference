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
    // 결과 저장 디렉토리가 없으면 생성(존재하면 무시).
    fs::create_directories(output_dir);

    // 이미지 목록 수집: 입력 디렉토리에서 지원 확장자만 필터링.
    std::vector<std::string> image_paths;
    for (auto& entry : fs::directory_iterator(images_dir)) {
        auto ext = entry.path().extension().string();
        // 대소문자 무관 비교를 위해 소문자로 정규화.
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext == ".jpg" || ext == ".jpeg" || ext == ".png" || ext == ".bmp") {
            image_paths.push_back(entry.path().string());
        }
    }
    // 결정적인 처리 순서를 위해 파일명 기준 정렬.
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

    // argv[1..3]는 위치 기반 필수 인자.
    std::string hef_path = argv[1];
    // images_dir / output_dir은 hailo_inference() 배치 경로용. 현재 main 흐름은 GUI만 사용.
    [[maybe_unused]] std::string images_dir = argv[2];
    [[maybe_unused]] std::string output_dir = argv[3];

    // video_path는 선택 인자 (GUI 재생용). 미지정 시 project_root 기준 기본값 사용.
    std::string video_path;
    if (argc >= 5) {
        video_path = argv[4];
    } else {
        // 실행 파일이 build/ 안에 있다고 가정하고 한 단계 위를 project_root로 본다.
        fs::path exe_dir = fs::path(argv[0]).parent_path();
        fs::path project_root = exe_dir.parent_path();
        video_path = (project_root / "resource" / "VIRAT_S_000001.mp4").string();
    }

    // QApplication 부트스트랩 후 메인 윈도우 띄우기.
    GuiApp gui_app(argc, argv);

    return gui_app.run(hef_path, video_path);
}
