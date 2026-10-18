add_library(nsm_platform STATIC
  theme.cpp
  strings.cpp
)
target_include_directories(nsm_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
