# SAGE System Architecture

## Data Flow Pipeline

```
┌─────────────────────────────────────────────────────────────────────┐
│                         EXCHANGE LAYER                               │
│  Binance    Coinbase    Kraken    Bybit    OKX    Bitfinex         │
└────────────────────────────┬────────────────────────────────────────┘
                             │ WebSocket / REST
                             ▼
┌─────────────────────────────────────────────────────────────────────┐
│  CAL — Connector Abstraction Layer                                  │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐             │
│  │  WebSocket   │  │ JSON Parser  │  │  Validator   │             │
│  │   Client     │→ │  (simdjson)  │→ │ (Price/Qty)  │             │
│  └──────────────┘  └──────────────┘  └──────────────┘             │
└────────────────────────────┬────────────────────────────────────────┘
                             │ Ring Buffer (Lock-Free SPSC)
                             ▼
┌─────────────────────────────────────────────────────────────────────┐
│  ADE — Analytics & Decision Engine                                  │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐             │
│  │ Tick Buffer  │  │Rolling Stats │  │ Normalizer   │             │
│  │ (Per Symbol) │→ │   (O(1))     │→ │ (Z-Score)    │             │
│  └──────────────┘  └──────────────┘  └──────────────┘             │
└────────────────────────────┬────────────────────────────────────────┘
                             │ Ring Buffer
                             ▼
┌─────────────────────────────────────────────────────────────────────┐
│  MIND — Intelligence Layer (Future)                                 │
│  ┌──────────────┐  ┌──────────────┐                                │
│  │ ONNX Runtime │→ │   Inference  │                                │
│  │   Session    │  │   (ML Model) │                                │
│  └──────────────┘  └──────────────┘                                │
└────────────────────────────┬────────────────────────────────────────┘
                             │ Ring Buffer
                             ▼
┌─────────────────────────────────────────────────────────────────────┐
│  RME — Risk Management Engine                                       │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐             │
│  │  Position    │  │ Risk Limits  │  │   Circuit    │             │
│  │   Tracker    │→ │   Checker    │→ │   Breaker    │             │
│  └──────────────┘  └──────────────┘  └──────────────┘             │
└────────────────────────────┬────────────────────────────────────────┘
                             │ Ring Buffer (Approved Orders Only)
                             ▼
┌─────────────────────────────────────────────────────────────────────┐
│  POE — Order Execution Engine                                       │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐             │
│  │  Order ID    │  │ Audit Logger │  │ FIX Encoder  │             │
│  │  Generator   │→ │  (Immutable) │→ │  (Protocol)  │             │
│  └──────────────┘  └──────────────┘  └──────────────┘             │
└────────────────────────────┬────────────────────────────────────────┘
                             │ TCP/FIX
                             ▼
┌─────────────────────────────────────────────────────────────────────┐
│                      EXCHANGE GATEWAY                                │
│                     (Order Submission)                               │
└─────────────────────────────────────────────────────────────────────┘
```

## Memory Layout

### Ring Buffer Structure (64-byte aligned)

```
┌─────────────────────────────────────────────────────────────┐
│  Cache Line 0 (64B)                                         │
│  ┌──────────────────────────────────────────────────────┐   │
│  │  atomic<size_t> head                                 │   │
│  │  padding[56]                                         │   │
│  └──────────────────────────────────────────────────────┘   │
├─────────────────────────────────────────────────────────────┤
│  Cache Line 1 (64B)                                         │
│  ┌──────────────────────────────────────────────────────┐   │
│  │  atomic<size_t> tail                                 │   │
│  │  padding[56]                                         │   │
│  └──────────────────────────────────────────────────────┘   │
├─────────────────────────────────────────────────────────────┤
│  Data Buffer (N × 64B messages)                             │
│  ┌──────────────────────────────────────────────────────┐   │
│  │  SageMessage[0]  (timestamp, seq, type, payload)    │   │
│  │  SageMessage[1]                                      │   │
│  │  ...                                                 │   │
│  │  SageMessage[N-1]                                    │   │
│  └──────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────┘
```

### SageMessage Layout (64 bytes)

```
Offset  Size  Field
------  ----  -----
0       8     timestamp_hw (TSC)
8       8     seq_id
16      1     msg_type
17      7     reserved (padding)
24      40    payload (union)
              - MarketData (price, qty, symbol_id)
              - Signal (symbol_id, side, confidence)
              - OrderReq (order_id, price, qty)
              - RiskAlert (level, reason[32])
------  ----
Total:  64    (1 cache line)
```

## Process Isolation

Each component runs as an independent process:

```
┌─────────────┐   ┌─────────────┐   ┌─────────────┐   ┌─────────────┐
│   sage_cal  │   │   sage_ade  │   │   sage_rme  │   │   sage_poe  │
│   (PID 100) │   │   (PID 101) │   │   (PID 102) │   │   (PID 103) │
└──────┬──────┘   └──────┬──────┘   └──────┬──────┘   └──────┬──────┘
       │                 │                 │                 │
       └─────────────────┴─────────────────┴─────────────────┘
                              │
                    Shared Memory (/dev/shm)
                    Ring Buffers (Lock-Free)
```

**Benefits:**
- Crash in one component doesn't affect others
- Independent restart/upgrade
- CPU core pinning per process
- Memory isolation

## CPU Core Assignment (Production)

```
Core 0: OS, SSH, Background Tasks
Core 1: CAL (Network I/O)
Core 2: ADE (Computation)
Core 3: RME (Risk Checks)
Core 4: POE (Order Execution)
Core 5: Monitoring, Logging
```

## Latency Budget (Target)

```
Exchange → CAL:           Network latency (variable)
CAL → ADE:                <50ns   (ring buffer)
ADE Processing:           <1μs    (feature calc)
ADE → MIND:               <50ns   (ring buffer)
MIND Processing:          <100μs  (inference)
MIND → RME:               <50ns   (ring buffer)
RME Processing:           <100ns  (risk check)
RME → POE:                <50ns   (ring buffer)
POE Processing:           <500ns  (audit + encode)
POE → Exchange:           Network latency (variable)
─────────────────────────────────────────────────
Total Internal Latency:   ~102μs  (excluding network)
```

## Failure Modes & Recovery

| Failure | Detection | Response |
|---------|-----------|----------|
| Exchange disconnect | Heartbeat timeout | Reconnect (exponential backoff) |
| Ring buffer full | `try_push()` returns false | Drop + increment counter + alert |
| Risk limit breach | Position check fails | Reject order + log |
| Daily loss limit | PnL check fails | Trip circuit breaker + halt trading |
| Component crash | Heartbeat loss | Alert operator + restart component |
| Data corruption | Validation fails | Reject + log + increment error counter |

## Observability

### Metrics (Per Component)

```cpp
struct Metrics {
    uint64_t messages_processed;
    uint64_t messages_dropped;
    uint64_t errors;
    uint64_t latency_p50_ns;
    uint64_t latency_p99_ns;
    uint64_t last_heartbeat_ts;
};
```

### Audit Trail

All critical events logged to `sage_audit.log`:
- Order submissions (BEFORE network send)
- Order fills
- Risk violations
- Circuit breaker trips
- Component shutdowns

Format: `YYYY-MM-DD HH:MM:SS | EVENT | Details`

## Configuration

`config/sage.toml`:
```toml
[risk]
max_position_per_symbol = 1000000
max_total_exposure = 100000.0
daily_loss_limit = 10000.0

[buffers]
ring_buffer_size = 1048576  # Must be power of 2
```

## Shutdown Sequence

1. Operator sends `SIGTERM` to any component
2. Component sets `shutdown_requested` flag
3. Stop accepting new data
4. Drain ring buffers
5. Cancel open orders (POE)
6. Flush audit logs
7. Release shared memory
8. Exit with status code 0

**Guarantee**: No data loss, all orders audited.
