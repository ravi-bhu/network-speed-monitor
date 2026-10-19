add_library(nsm_report STATIC
  report_model.cpp
  csv_export.cpp
)
target_include_directories(nsm_report PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(nsm_report PUBLIC nsm_storage nsm_core)
