# SAGE Core Library
# Header-only library with system utilities

add_library(sage_core INTERFACE)

target_include_directories(sage_core INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Link platform libraries
target_link_libraries(sage_core INTERFACE
    ${SAGE_PLATFORM_LIBS}
)
