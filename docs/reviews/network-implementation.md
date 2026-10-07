# Network implementation review: TASK-002 and TASK-009

Status: implemented and validated with Windows localhost traffic. Physical 2.5G, two-machine performance, NIC/kernel drop attribution, sustained disk capture, and clean-machine deployment remain unverified; these results do not certify a physical network interface.

## Files and contracts

Product files owned and changed by this worker:

- `src/network/network_engine.cpp`: standalone Asio 1.30.2 implementation of the authoritative `NetworkEngine` header.
- `tests/test_network.cpp`: ordinary console executable, no Qt Test or GUI dependency.
- `tools/network_bench.cpp`: genuine headless sender, receiver, and localhost self-loop benchmark.
- `docs/reviews/network-implementation.md`: this report.

No shared headers, root CMake, session, UI, or Git commits were changed. Temporary standalone CMake, verification scripts, executables, and JSON evidence are under `build/network-owner/`; they are reproducible build artifacts, not product sources.

The engine owns one Asio I/O execution thread. `start`, `stop`, `send`, and selective disconnect post asynchronous work; connecting and resolving never block the caller. Destruction cancels all operations, drains canceled completions, and joins the execution thread. Callbacks run exclusively on that thread and should remain brief; the owner must destroy the engine outside its own callback. Asio's underlying OS resolver can take time to finish a canceled system DNS operation; the connect deadline suppresses its eventual result but cannot guarantee an OS resolver thread finishes instantly.

A generation is invalidated immediately at every start/stop request. Resolution, connection, timer, accept, receive, and send handlers check generation and connection liveness. An obsolete posted start/stop cannot close a newer generation. A TCP block rejected by the consumer is retained with identical shared payload, timestamp, sequence, endpoint, and connection ID; a 5-ms timer retries it without issuing another read. UDP consumer rejection discards only that datagram and continues, with drop accounting belonging to the consumer as specified by the header. Completed TX is never retried when the consumer returns false.

## Transport behavior and bounds

| Area | Implemented behavior |
| --- | --- |
| TCP client | IPv4 asynchronous hostname resolution, deadline covering resolution/connect, cancellation, explicit local address/port binding, endpoint reporting, and retries of resolved IPv4 endpoints that preserve local binding. |
| TCP server | Stable monotonic engine-lifetime client IDs, per-client queues and receive state, directed sends, snapshot broadcasts, selective disconnect, and listener shutdown closing managed clients. Four simultaneous clients are tested. A 256-client limit bounds receive state. |
| UDP | Independent local bind and destination configuration, asynchronous IPv4 destination hostname resolution, actual bound port, source address/port per datagram, zero-length receive/TX, and legal 65,507-byte IPv4 payloads. Binding does not claim a reachable peer. Resolution failure leaves a receive-only bound socket usable. |
| Send admission | Synchronous bounded reservation before posting. Charges retained vector capacity, multiplied by the broadcast target count, plus a minimum unit for empty UDP. A separate 4,096-descriptor cap bounds tiny/empty writes. `pendingSendBytes()` reports queued/in-flight payload bytes. Overflow returns false and posts a coalesced `SendRejected` event; rejection itself cannot create an unlimited notification queue. |
| TCP send | `asio::async_write` serializes full writes per connection. TX is emitted only for bytes actually completed. A partial/error completion records only the completed prefix and emits the precise count and OS error before disconnecting. A local completion is not proof of peer receipt. |
| UDP send | Full datagram completion is required for TX; oversized IPv4 payloads are rejected at admission. Errors and incomplete completion lengths are visible. |
| Buffers | A 65,536-byte UDP receive scratch buffer preserves legal datagrams. Retained payloads use pooled capacities 0/256/2,048/8,192/65,536, with at most 32 idle buffers per class (about 2.32 MiB total). Small TCP blocks are also repacked. Buffers are recycled only after all consumers release them. This deliberately trades a copy of small payloads for bounded retained capacity; it is not a zero-copy claim. Downstream consumers must bound their own retained records and account capacity and metadata. |
| Socket settings | Requested receive/send sizes are set and actual OS values read with `get_option`. Server snapshots reflect the listener before clients arrive and the most recently configured accepted socket thereafter. |
| Events | Connecting, connected, bound, listening, client addition/removal, disconnect, rejection, and precise operation/category/code errors are reported with available endpoints. Windows ANSI system error messages are converted to UTF-8. Intentional TCP closure calls socket shutdown before close. |

## Build and validation commands

Toolchain: Windows 11 build 26100; x86-64 Intel family 6 model 197, 16 logical CPUs; Qt's `C:/Qt/Tools/mingw1310_64/bin/g++.exe`, GCC 13.1.0, and `C:/Qt/Tools/Ninja/ninja.exe`. The network library, tests, and benchmark do not link Qt. Runtime PATH needs the MinGW bin directory.

The temporary standalone target was built first while other owners were implementing their sources:

```sh
cmake -S build/network-owner -B build/network-owner/out -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_COMPILER=C:/Qt/Tools/mingw1310_64/bin/g++.exe \
  -DCMAKE_MAKE_PROGRAM=C:/Qt/Tools/Ninja/ninja.exe
cmake --build build/network-owner/out
PATH='/c/Qt/Tools/mingw1310_64/bin':"$PATH" \
  ctest --test-dir build/network-owner/out --output-on-failure
```

Root integration targets were independently configured and built using the coordinator-owned CMake without editing it:

```sh
cmake -S . -B build/network-owner/integration -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_COMPILER=C:/Qt/Tools/mingw1310_64/bin/g++.exe \
  -DCMAKE_MAKE_PROGRAM=C:/Qt/Tools/Ninja/ninja.exe \
  '-DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/mingw_64;C:/Users/threeTeeth/workSpace/github/PortBridge/.deps/qtserialport-install' \
  -DQt6SerialPort_DIR=C:/Users/threeTeeth/workSpace/github/PortBridge/.deps/qtserialport-install/lib/cmake/Qt6SerialPort
cmake --build build/network-owner/integration --target test_network portbridge_bench
PATH='/c/Qt/Tools/mingw1310_64/bin':"$PATH" \
  ctest --test-dir build/network-owner/integration -R '^network$' --output-on-failure
```

In this Qt installation `CMAKE_PREFIX_PATH` alone failed to locate the separately installed SerialPort component; the explicit component directory fixed configuration. No network dependency issue was involved. The root configure emitted an optional Vulkan-header notice but succeeded.

Both final builds compile with no warnings in the network owner targets. Final standalone CTest passed in 1.48 seconds; final integration CTest passed in 1.62 seconds (one registered console executable containing six groups). Earlier lifecycle/transport tests also passed five consecutive runs using `--repeat until-fail:5` before the additional generation and size-class assertions were added.

The six groups exercise:

1. A deterministic 180,123-byte TCP stream with arbitrary write sizes; exact byte order and content, repeated rejection of an identical retained block, local binding, actual buffers, and completed TX.
2. Four clients with endpoint-matched stable IDs; directed data reaches only its target, broadcast reaches all four, receive identities remain separate, and selective disconnect leaves another client usable.
3. Empty UDP, two independent source ports, preserved boundaries, the maximum legal 65,507-byte payload, zero-length TX, rejection of 65,508 bytes, small retained capacities, and continued receipt after eight consumer rejections.
4. A held I/O callback makes queue saturation deterministic: two 32-byte writes fill a 64-byte budget, oversized retained capacity is rejected, 10,000 further requests are rejected, notifications remain bounded, and a false TX consumer never causes retransmission.
5. A backpressured TCP block is retired while a new UDP generation is started; old retry/data callbacks cannot enter the replacement session.
6. Cancel during Connecting, an armed 1-ms deadline with deliberately delayed callback dispatch, precise local connection refusal, 25 complete start/stop cycles, rapid generation changes, and no callback after destruction. The deterministic deadline test validates the timer/cancellation path without depending on an external blackhole address; an actual physical-link timeout is not claimed.

## Headless benchmark and results

Examples, from the repository root with the MinGW runtime on PATH:

```sh
build/network-owner/out/portbridge_bench.exe --help
build/network-owner/out/portbridge_bench.exe --mode self-loop --protocol tcp --payload 4096 --rate 5000 --duration 1
build/network-owner/out/portbridge_bench.exe --mode self-loop --protocol udp --payload 1472 --rate 10000 --duration 1
build/network-owner/out/portbridge_bench.exe --mode self-loop --protocol tcp --payload 4096 --rate 0 --duration 0.5 --queue-bytes 4096
python build/network-owner/verify_bench.py
```

The protocol carries a 24-byte header: magic at offset 0, big-endian frame length at 4, 64-bit sequence at 8, 32-bit FNV-1a checksum at 16 (checksum field treated as zero when hashing), and sender stream ID at 20. Remaining bytes are a deterministic sequence/offset/stream pattern. TCP reassembly is independent of receive block boundaries. UDP uses one complete frame per datagram. `--payload` includes this header; `--rate` is frames/datagrams per second, and zero means unpaced. Rejected admission retries the same frame sequence rather than silently introducing an artificial network gap.

Final verification artifacts: `build/network-owner/benchmark-results.json`, produced by the temporary Python driver. Measured receive throughput uses total elapsed time including drain, so values are lower than a simple requested-rate multiplication. All entries below are **localhost**, with recording/UI disabled, not NIC performance:

| Protocol / payload | Requested rate / duration | TX completed / RX unique | Receive bytes/s | Missing / invalid |
| --- | --- | --- | --- | --- |
| TCP / 4,096 B | 5,000 frames/s / 1 s | 5,000 / 5,000 | 20,212,932.586 | 0 / 0 |
| UDP / 64 B | 1,000 datagrams/s / 1 s | 1,000 / 1,000 | 62,964.771 | 0 / 0 |
| UDP / 1,472 B | 10,000 datagrams/s / 1 s | 10,000 / 10,000 | 14,497,298.302 | 0 / 0 |
| UDP / 65,507 B | 100 datagrams/s / 1 s | 100 / 100 | 6,486,571.163 | 0 / 0 |

Separate console processes were also tested for both TCP and UDP: 256-byte frames, 1,000 frames/s, one-second sender and two-second receiver plus drain. Each sender completed 1,000 frames and its receiver validated 1,000 unique frames with zero corruption or observed gaps. This exercises the actual `send` and `receive` modes, not only the self-loop mode.

A controlled UDP sender then injected sequences `0,2,1,2,4` and a corrupted sequence-3 frame, with receiver expected count 5. JSON correctly reports exactly one duplicate, one reordered frame, one missing frame, and one checksum/format failure; the process exits 2. This demonstrates that the integrity and loss fields are derived from real bytes rather than hard-coded counters.

The unpaced TCP run with a 4,096-byte queue completed and verified 8,434 frames / 34,545,664 bytes, reported 682,990 rejected admission attempts, peak pending 4,096 bytes, zero missing/corrupt data, and 66,688,115.072 receive bytes/s including drain. Its full JSON is `build/network-owner/benchmark-unpaced.json`. These rejection counts include retries and are not a count of lost transmitted frames; unpaced retry polling consumes CPU. This short local run is not a sustained-throughput or hardware claim.

The benchmark emits one JSON object on stdout and readiness on stderr. It reports completed TX, RX bytes/datagrams, unique/invalid frames, duplicates, reordered and late frames, known/observed gaps, throughput, queue peak, actual OS buffers, transport errors, teardown errors, and drain status. Runtime transport errors and intentional teardown errors are counted separately; cancellation at teardown cannot imply data completion. No application receive queue is used, so application queue drops are zero; NIC/kernel losses remain `null`. UDP end-to-end loss is an observed metric, not an automatic tool-execution failure. TCP self-loop consistency failures, invalid frames, incomplete TCP frames, undrained writes, and runtime errors make the result unsuccessful.

Sequence observation is bounded to 65,536 positions per sender identity and 256 sender identities. Older late arrivals are marked unclassified rather than falsely called duplicates. Loss precision becomes false and loss fraction becomes null when this prevents an exact count. Self-loop uses completed TX as the expected total. A standalone receiver knows trailing loss only if `--expected` supplies the actual sender total; otherwise only gaps through the highest observed sequence are known, and `expected_frames_known`/`loss_count_exact` remain false. Multiple physical senders need separately reconciled manifests.

## Physical-link usage and remaining evidence

For two-machine work, run a receiver bound to its actual local IPv4 or `0.0.0.0`, and a sender addressed to that machine; use matching protocol and payload, and allow the receiver a longer duration than the sender:

```sh
# Receiving machine
portbridge_bench.exe --mode receive --protocol udp --bind 0.0.0.0 --port 19090 --payload 1472 --duration 15 --drain-ms 2000
# Sending machine: replace the address with the actual receiving machine IPv4
portbridge_bench.exe --mode send --protocol udp --bind 0.0.0.0 --host 192.168.1.20 --port 19090 --payload 1472 --rate 10000 --duration 10
```

`192.168.1.20` is an illustrative command argument, not a tested host. Repeat TCP, UDP payloads 64/256/1,024/1,472, sustained load levels, burst traffic, and the required recording/UI combinations. Compare receiver unique frames with sender completed frames to include trailing loss; do not treat planned rate times duration as proof of transmitted count. Capture CPU/RSS, OS/NIC counters, MTU/offload/driver details, power settings, and disk speed separately. The tool does checksum generation/validation and TCP frame assembly, so its CPU overhead must also be measured before attributing limits to Asio or the NIC.

Physical NIC negotiation, two-machine 2.5G sustained capacity, high-PPS/burst loss envelopes, simultaneous bidirectional and cumulative multiclient throughput, disk-capture saturation, UI responsiveness under those loads, and clean-system deployment were not measured by this worker. IPv6 and alternative Qt/native network backend comparisons are outside this implementation's IPv4-first scope. Hardware latency percentiles are not claimed without synchronized clocks.
