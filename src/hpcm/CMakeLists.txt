# SAGE HPCM Library
# Header-only library with high-performance math

add_library(sage_hpcm INTERFACE)

target_include_directories(sage_hpcm INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(sage_hpcm INTERFACE
    sage_core
)
