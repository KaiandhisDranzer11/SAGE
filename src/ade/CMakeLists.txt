# SAGE ADE - Analytics & Decision Engine

add_executable(sage_ade ade_main.cpp)

target_link_libraries(sage_ade PRIVATE
    sage_core
    sage_types
    sage_infra
    sage_hpcm
    ${SAGE_PLATFORM_LIBS}
)
