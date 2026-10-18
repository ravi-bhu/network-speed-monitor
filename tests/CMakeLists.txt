add_executable(nsm_tests
  test_main.cpp
  test_units.cpp
  test_counter_tracker.cpp
  test_speed_history.cpp
  test_overlay_layout.cpp
)
target_include_directories(nsm_tests PRIVATE ${CMAKE_SOURCE_DIR}/third_party/doctest)
target_link_libraries(nsm_tests PRIVATE nsm_core)
add_test(NAME nsm_unit COMMAND nsm_tests -tse=integration)
