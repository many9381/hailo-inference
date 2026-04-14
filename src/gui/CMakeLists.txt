target_sources(hailo_server PRIVATE
    GuiApp.cpp
    GuiApp.h
)

target_sources(hailo_client PRIVATE
    GuiApp.cpp
    GuiApp.h
)

add_subdirectory(server)
add_subdirectory(client)
