add_library(nsm_core STATIC
  units.cpp
  counter_tracker.cpp
  sampler.cpp
  speed_history.cpp
  overlay_layout.cpp
)
target_include_directories(nsm_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
