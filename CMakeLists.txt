cmake_minimum_required(VERSION 3.20)
project(SAGE VERSION 1.0.0 LANGUAGES CXX)
# ============================================================================
# Build Configuration
# ============================================================================

# Default to Release build
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Append local cmake modules
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# ============================================================================
# Dependencies
# ============================================================================

include(Dependencies)

# ============================================================================
# Compiler Flags
# ============================================================================

include(CompilerFlags)

# ============================================================================
# Core Libraries (Header-only)
# ============================================================================

add_subdirectory(src/core)
add_subdirectory(src/types)
add_subdirectory(src/infra)
add_subdirectory(src/hpcm)

# ============================================================================
# Component Executables
# ============================================================================

add_subdirectory(src/cal)
add_subdirectory(src/ade)
add_subdirectory(src/rme)
add_subdirectory(src/poe)

# ============================================================================
# Tests
# ============================================================================

enable_testing()
add_subdirectory(tests)

# ============================================================================
# Build Summary
# ============================================================================

message(STATUS "")
message(STATUS "==========================================")
message(STATUS "SAGE Build Configuration")
message(STATUS "==========================================")
message(STATUS "Version:      ${PROJECT_VERSION}")
message(STATUS "Build Type:   ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Compiler:     ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "")
message(STATUS "Components:")
message(STATUS "  - CAL  (Connector Abstraction Layer)")
message(STATUS "  - ADE  (Analytics & Decision Engine)")
message(STATUS "  - HPCM (High-Performance Math)")
message(STATUS "  - RME  (Risk Management Engine)")
message(STATUS "  - POE  (Order Execution Engine)")
message(STATUS "")
message(STATUS "Optimizations:")
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    message(STATUS "  - O3 optimization enabled")
    message(STATUS "  - LTO enabled")
    message(STATUS "  - Native architecture targeting")
    message(STATUS "  - No exceptions/RTTI in hot path")
endif()
message(STATUS "==========================================")
message(STATUS "")
