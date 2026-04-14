# GuiApp 은 양쪽 역할을 런타임에 분기한다. server 역할 실행 함수는
# weak 기본 구현을 포함하므로 hailo_common 에 둔다 (ServerRole.cpp 가
# hailo_server 에서 강한 정의로 덮어씀).
target_sources(hailo_common PRIVATE
    GuiApp.cpp
    GuiApp.h
)

add_subdirectory(server)
add_subdirectory(client)
