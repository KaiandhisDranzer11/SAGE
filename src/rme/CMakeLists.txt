# SAGE RME - Risk Management Engine

add_executable(sage_rme rme_main.cpp)

target_link_libraries(sage_rme PRIVATE
    sage_core
    sage_types
    sage_infra
    ${SAGE_PLATFORM_LIBS}
)
