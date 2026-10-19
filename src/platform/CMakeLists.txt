add_library(nsm_platform STATIC
  theme.cpp
  strings.cpp
  adapters.cpp
  internet_route.cpp
  network_identity.cpp
)
target_include_directories(nsm_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(nsm_platform PUBLIC iphlpapi ole32 oleaut32 uuid)
