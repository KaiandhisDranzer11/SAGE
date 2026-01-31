# SAGE — System for Automated Generation & Execution

A low-latency, fault-tolerant, observable trading system built in C++20.

## Architecture

SAGE is a pipeline of isolated processes communicating via lock-free shared memory:

```
Exchange → CAL → ADE → MIND → RME → POE → Audit
```

### Components

- **CAL** (Connector Abstraction Layer): WebSocket ingestion, validation
- **ADE** (Analytics & Decision Engine): Feature extraction, normalization
- **HPCM** (High-Performance Math): SIMD operations, lookup tables
- **MIND** (Intelligence Layer): ML inference (ONNX Runtime)
- **RME** (Risk Management Engine): Position tracking, limit enforcement
- **POE** (Order Execution Engine): FIX protocol, audit logging

## Build Requirements

- **OS**: Linux x86-64 (Kernel 5.15+) or WSL2
- **Compiler**: GCC 11+ or Clang 14+
- **CMake**: 3.20+
- **Dependencies**: Boost 1.80+, simdjson, ONNX Runtime (optional)

## Build Instructions

```bash
# Create build directory
mkdir build && cd build

# Configure
cmake .. -DCMAKE_BUILD_TYPE=Release

# Build
make -j$(nproc)

# Run tests
./tests/test_core
```

## Running the System

Each component runs as a separate process:

```bash
# Terminal 1: CAL
./build/src/cal/sage_cal

# Terminal 2: ADE
./build/src/ade/sage_ade

# Terminal 3: RME
./build/src/rme/sage_rme

# Terminal 4: POE
./build/src/poe/sage_poe
```

## Key Design Principles

1. **Zero Allocation in Hot Path**: All memory pre-allocated at startup
2. **Fixed-Point Arithmetic**: Deterministic price calculations (no floats)
3. **Lock-Free IPC**: SPSC ring buffers with cache-line alignment
4. **Fail-Fast**: Invariant violations cause immediate termination
5. **Observable**: Every component emits metrics and heartbeats

## Performance Targets

| Metric | Target |
|--------|--------|
| Ring buffer write | <50ns p50 |
| CAL parse + validate | <500ns p50 |
| ADE feature calc | <1μs p50 |
| RME risk check | <100ns p50 |

## Configuration

Edit `config/sage.toml` for runtime parameters:
- Risk limits
- Exchange endpoints
- Buffer sizes

## Graceful Shutdown

Send `SIGTERM` or `SIGINT` (Ctrl+C) to any component. The shutdown manager will:
1. Stop ingestion
2. Drain buffers
3. Cancel open orders
4. Flush audit logs

## Audit Trail

All orders are logged to `sage_audit.log` BEFORE transmission to exchange.

## License

Apache 2.0
