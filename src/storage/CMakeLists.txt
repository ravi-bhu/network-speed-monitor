add_library(nsm_sqlite STATIC ${CMAKE_SOURCE_DIR}/third_party/sqlite/sqlite3.c)
set_target_properties(nsm_sqlite PROPERTIES LINKER_LANGUAGE C)
target_include_directories(nsm_sqlite PUBLIC ${CMAKE_SOURCE_DIR}/third_party/sqlite)
target_compile_definitions(nsm_sqlite PRIVATE SQLITE_OMIT_LOAD_EXTENSION SQLITE_DQS=0 SQLITE_THREADSAFE=0)
target_compile_options(nsm_sqlite PRIVATE /W0)

add_library(nsm_storage STATIC
  usage_store.cpp
)
target_include_directories(nsm_storage PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(nsm_storage PUBLIC nsm_core nsm_sqlite)
