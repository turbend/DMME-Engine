add_subdirectory(core/window)
add_subdirectory(core/renderer)

add_executable(dmme_engine WIN32 main.cpp)

target_link_libraries(dmme_engine PRIVATE
    dmme_window
    dmme_renderer
)

target_include_directories(dmme_engine PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)