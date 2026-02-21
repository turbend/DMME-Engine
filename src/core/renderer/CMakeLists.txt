add_library(dmme_renderer STATIC
    RenderPipeline.cpp
    GPUSurface.cpp
    FrameBuffer.cpp
    drivers/DX11Driver.cpp
    drivers/OpenGLDriver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../utils/Logger.cpp
)

target_include_directories(dmme_renderer PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/drivers
    ${CMAKE_CURRENT_SOURCE_DIR}/../../
)

target_link_libraries(dmme_renderer PUBLIC
    spdlog::spdlog
    dmme_window
)

target_link_libraries(dmme_renderer PRIVATE
    d3d11
    dxgi
    d3dcompiler
    dxguid
)