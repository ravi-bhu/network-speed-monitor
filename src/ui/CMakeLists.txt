# UI code lives in a library so the integration tests can link it; the exe is main.cpp + resources.
add_library(nsm_ui STATIC
  taskbar_probe.cpp
  overlay_renderer.cpp
  overlay_window.cpp
  taskbar_space_win10.cpp
  overlay_manager.cpp
  app.cpp
)
target_include_directories(nsm_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/.. PRIVATE ${CMAKE_SOURCE_DIR}/res)
target_link_libraries(nsm_ui PUBLIC nsm_core nsm_platform nsm_storage d2d1 dwrite ole32 oleaut32 shcore comctl32)

add_executable(NetworkSpeedMonitor WIN32
  main.cpp
  ${CMAKE_SOURCE_DIR}/res/app.rc
)
target_include_directories(NetworkSpeedMonitor PRIVATE ${CMAKE_SOURCE_DIR}/res)
target_link_libraries(NetworkSpeedMonitor PRIVATE nsm_ui comctl32)
# The manifest is embedded by res/app.rc; stop the linker from generating a second one.
target_link_options(NetworkSpeedMonitor PRIVATE /MANIFEST:NO)
