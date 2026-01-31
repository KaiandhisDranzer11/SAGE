# SAGE Types Library
# Header-only library with data types

add_library(sage_types INTERFACE)

target_include_directories(sage_types INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(sage_types INTERFACE
    sage_core
)
