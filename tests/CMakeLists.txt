add_executable(nsm_tests
  test_main.cpp
  test_units.cpp
  test_counter_tracker.cpp
  test_speed_history.cpp
  test_overlay_layout.cpp
  test_taskbar_probe.cpp
  test_overlay_renderer.cpp
  test_strings.cpp
  test_taskbar_space_win10.cpp
  test_overlay_manager.cpp
  test_adapters.cpp
  ${CMAKE_SOURCE_DIR}/res/strings.rc
)
target_include_directories(nsm_tests PRIVATE ${CMAKE_SOURCE_DIR}/third_party/doctest ${CMAKE_SOURCE_DIR}/res)
target_link_libraries(nsm_tests PRIVATE nsm_core nsm_platform nsm_ui)
add_test(NAME nsm_unit COMMAND nsm_tests -tse=integration)
