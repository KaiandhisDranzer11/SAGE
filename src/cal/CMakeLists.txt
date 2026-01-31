# SAGE CAL - Connector Abstraction Layer

add_executable(sage_cal cal_main.cpp)

target_link_libraries(sage_cal PRIVATE
    sage_core
    sage_types
    sage_infra
    ${SAGE_PLATFORM_LIBS}
)

# Enable warnings
target_compile_options(sage_cal PRIVATE -Wno-unused-function)
