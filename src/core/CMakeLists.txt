add_library(nsm_core STATIC
  units.cpp
  counter_tracker.cpp
  sampler.cpp
  speed_history.cpp
  overlay_layout.cpp
  civil_date.cpp
  time_zone.cpp
  periods.cpp
  usage_accumulator.cpp
  utf8.cpp
)
target_include_directories(nsm_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
