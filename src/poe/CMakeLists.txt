# SAGE POE - Order Execution Engine

add_executable(sage_poe poe_main.cpp)

target_link_libraries(sage_poe PRIVATE
    sage_core
    sage_types
    sage_infra
    ${SAGE_PLATFORM_LIBS}
)
