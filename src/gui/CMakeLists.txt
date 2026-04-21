# GuiApp dispatches between both roles at runtime. The server-role entry
# function is kept in hailo_common because it includes the weak default
# implementation (overridden by a strong definition in hailo_server via ServerRole.cpp).
target_sources(hailo_common PRIVATE
    GuiApp.cpp
    GuiApp.h
)

add_subdirectory(server)
add_subdirectory(client)
