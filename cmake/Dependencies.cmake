# SAGE External Dependencies

# Threads
find_package(Threads REQUIRED)

# Optional: Boost (for production WebSocket)
# find_package(Boost 1.80 COMPONENTS system REQUIRED)

# Optional: simdjson (for production JSON parsing)
# find_package(simdjson CONFIG)

# Optional: ONNX Runtime (for MIND component)
# find_package(onnxruntime CONFIG)

# Check for NUMA support
include(CheckIncludeFile)
check_include_file("numa.h" HAVE_NUMA_H)
if(HAVE_NUMA_H)
    message(STATUS "NUMA support: enabled")
    add_definitions(-DSAGE_HAS_NUMA)
else()
    message(STATUS "NUMA support: disabled")
endif()

# Check for huge pages
check_include_file("sys/mman.h" HAVE_SYS_MMAN_H)
if(HAVE_SYS_MMAN_H)
    message(STATUS "Huge pages support: enabled")
endif()

# Linux-specific libraries
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(SAGE_PLATFORM_LIBS rt pthread)
else()
    set(SAGE_PLATFORM_LIBS pthread)
endif()
