add_library(dmme_window STATIC
    TransparentWindow.cpp
    ClickThrough.cpp
    OpacityController.cpp
    MultiMonitor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../utils/Logger.cpp
)

target_include_directories(dmme_window PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../
)

target_link_libraries(dmme_window PUBLIC
    spdlog::spdlog
)

target_link_libraries(dmme_window PRIVATE
    user32
    gdi32
    dwmapi
    shcore
)