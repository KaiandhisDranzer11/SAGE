# SAGE Infrastructure Library
# Header-only library with ring buffers and IPC

add_library(sage_infra INTERFACE)

target_include_directories(sage_infra INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(sage_infra INTERFACE
    sage_core
    sage_types
)
