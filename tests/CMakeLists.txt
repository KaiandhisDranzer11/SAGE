# SAGE Tests

enable_testing()

# Core tests
add_executable(test_core test_core.cpp)
target_link_libraries(test_core
    sage_core
    sage_types
    sage_infra
)

# Add test
add_test(NAME core_tests COMMAND test_core)

# Audit durability tests
add_executable(test_audit_durability audit_durability_test.cpp)
target_link_libraries(test_audit_durability
    sage_core
    sage_types
    sage_infra
)

add_test(NAME audit_durability_tests COMMAND test_audit_durability)

# Latency benchmark (separate executable)
add_executable(benchmark_latency test_core.cpp)
target_link_libraries(benchmark_latency
    sage_core
    sage_types
    sage_infra
)
target_compile_definitions(benchmark_latency PRIVATE SAGE_BENCHMARK)
