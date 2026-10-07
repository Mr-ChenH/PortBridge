# Audit repair: network G02 and preliminary independent acceptance review

Date: 2026-10-07. Network G02 is implemented and the isolated product network target passes all **nine** permanent regression groups. This report covers the network repair and early independent feedback sent while session/UI owners were still implementing; it is **not** final independent acceptance of their completed changes.

## Ownership and implementation

Changed product files:

- `src/network/network_engine.cpp`
- `src/network/udp_receive.hpp` (new private shared completion helper)
- `tests/test_network.cpp`
- `docs/reviews/audit-fix-network.md` (this report)

No UI, session, shared types, root CMake, benchmark, release build, or package files were edited. No additional agents were started. The session owner supplied the appended `EventKind::ReceiveTruncated` contract and the statistics field; those shared changes are not network-worker edits.

The actual `async_receive_from` completion now delegates to `network_detail::completeUdpReceive`. An `asio::error::message_size` completion:

1. Emits exactly one structured `ReceiveTruncated` event, with connection ID 0, received source address/port, actual local binding, and the same operation/error message/category/numeric code formatter used before the repair. Windows production error formatting still converts ANSI system text to UTF-8.
2. Never invokes datagram delivery, even when completion reports a positive byte count; scratch bytes are not allocated, recorded, or admitted as a full datagram.
3. Resumes receive on the same binding without fail/disconnect or changes to send readiness/reservations.

Production still uses its **65,536-byte** UDP scratch buffer. No buffer was reduced to induce truncation. Success, empty datagrams, transient Winsock ICMP notifications, and fatal-error behavior use the same completion dispatch; all existing outer generation gates, pool ownership, receive admission, and backpressure behavior remain in place.

There are no `EventKind` switches in network ownership. Session event dispatch/SYSTEM event naming and UI diagnostics were explicitly flagged to the coordinator. The session owner has supplied a dedicated switch case and name entry in the in-progress implementation, but their final statistics/UI behavior requires separate acceptance.

## Meaningful regression and simulation boundary

`udpTruncationCompletionContinues` in `tests/test_network.cpp` uses two actual IPv4 localhost UDP sockets and a 65,536-byte receive array. The first receive succeeds with actual nonempty scratch bytes and the actual sender endpoint. The test replaces **only that first completion's error code** with `asio::error::message_size`, then calls the same private completion helper used by production.

The test asserts no positive-length partial delivery, a distinct single nonterminal event, operation/message/category/code detail, exact source and bound local endpoint, and no confusion with an unrelated configured TX destination. The helper's continuation arms the next actual async receive and sends a second datagram. That second completion retains its real OS result, and the regression verifies exact bytes and source, two continuation calls, zero fatal calls, and the unchanged open socket/local binding. A three-second deadline prevents hangs.

This does **not** claim the OS physically generated a truncated legal IPv4 packet. The legal IPv4 UDP payload maximum, 65,507 bytes, fits the production buffer, so shrinking the production buffer would weaken normal behavior merely to exercise an uncommon result. The test verifies production-shared classification and dispatch with realistic positive completion bytes, plus actual subsequent socket receive. It does not inject into an entire NetworkEngine instance or test a physical NIC/driver truncation. The separate existing NetworkEngine regression receives and verifies a real maximum-sized 65,507-byte datagram, and the eight existing engine/pool groups still execute unchanged.

## Build and test evidence

Configured the real root project into its isolated `build/audit-network` directory with Qt 6.8.3, MinGW 13.1, Ninja, and the project SerialPort installation:

```sh
cmake -S . -B build/audit-network -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_COMPILER=C:/Qt/Tools/mingw1310_64/bin/g++.exe \
  -DCMAKE_MAKE_PROGRAM=C:/Qt/Tools/Ninja/ninja.exe \
  '-DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/mingw_64;C:/Users/threeTeeth/workSpace/github/PortBridge/.deps/qtserialport-install' \
  -DQt6SerialPort_DIR=C:/Users/threeTeeth/workSpace/github/PortBridge/.deps/qtserialport-install/lib/cmake/Qt6SerialPort
cmake --build build/audit-network --target test_network -j 4
PATH='/c/Qt/Tools/mingw1310_64/bin':"$PATH" \
  ctest --test-dir build/audit-network -R '^network$' --output-on-failure
```

Configuration and target build succeeded with no network/test compiler warnings. CMake emitted its existing optional Vulkan-header notice. CTest: **network 1/1 passed, 1.48 seconds** (1.50 seconds including CTest overhead). Full nine-group stdout is in `build/audit-network/Testing/Temporary/LastTest.log`:

| Group | Result |
| --- | --- |
| Retained payload outlives pool, retired cache expires, reuse after last owner | PASS |
| TCP arbitrary splits, identical retained-block backpressure, exact stream/TX | PASS |
| Four TCP clients, directed/broadcast, selective disconnect | PASS |
| UDP empty/maximal/multiple sources, capacity charging, consumer drops | PASS |
| Simulated message_size in production helper, prefix rejected, next actual UDP receive | PASS |
| Native Windows absent UDP peer: binding, independent receive, completed TX survive | PASS |
| Bounded admission, 10,000 overflow attempts, completed TX never retried | PASS |
| Retired TCP backpressure cannot enter replacement UDP generation | PASS |
| Cancel/deadline/refusal, repeated lifecycle changes, callback quiescence | PASS |

Only the owned network target was built/tested in this phase. Full session/UI integration, physical truncation, hardware serial, physical 2.5G, sustained load, deployment, and screenshots are not certified by this evidence.

## Early independent feedback delivered to coordinator

These observations were sent through Orca status messages before completion, without editing other owners' files. They refer to sources observed during an active repair wave; they must be checked against final sources rather than automatically treated as remaining final defects.

### Concrete edges

- **Benchmark visibility after enum change:** `tools/network_bench.cpp` counts transport errors only for `EventKind::Error`. Appending `ReceiveTruncated` means a truncation no longer enters those metrics. The coordinator owns the decision to add a dedicated truncation metric or otherwise report it; the benchmark was outside this worker's edit scope.
- **Continuous-send counter:** `SessionController::Impl::sent` was signed `int`, incremented at every successful periodic tick. The newly exposed continuous count=0 UI path can eventually overflow. Saturating/widening the counter avoids undefined behavior while preserving finite-count termination. This is a source-level edge, not a multi-day runtime reproduction.
- **Malformed catalog numbers:** the initial loader checked JSON string types but ignored `toULongLong` parse success, allowing invalid strings to become fabricated zero metadata. The subsequent in-progress recorder update now checks numeric parse results and transport-summary values; final tests should cover malformed and legacy sidecars and preserve unknown metadata.
- **Late old-profile writer failure:** the updated recorder used epochs for flushed byte/record totals, but its writer error lambda still unconditionally incremented `state_.failures` and replaced `state_.error`. `resetStatistics(true)` hides such old-profile failures through `isolated_`; a later `start` cleared `isolated_` without clearing the hidden failure totals. A late old writer finalization/catalog error could therefore reappear when recording in the new profile. Requested a writer/profile error fence or clearing hidden state when leaving isolation, with same-profile cumulative semantics preserved. This is source evidence, not a forced slow-filesystem reproduction in this phase.
- **Catalog scanner thread creation:** the initial persistent scanner thread creation lacked a construction-failure handler. This is a lower-priority resource-exhaustion edge and was identified explicitly as such.

### Acceptance cases requiring final evidence

| Area | Concrete acceptance needs |
| --- | --- |
| A01/A02/A07 profile lifecycle | Ordinary switch, create, edit transport, delete selected profile, import replacement profiles, and constructor selection must synchronize controller configuration; clear/reset statistics, errors, model/stream/inspection/selection; suppress retired generations; leave activity stopped. Clearing must occur after synchronous stop signals to avoid immediate repopulation. |
| A03 valid unsaved edits | Modify current UDP port, create a new profile, return to old profile and verify the modified port persisted. Cover cancel and invalid-edit policy without silent valid-edit loss. |
| A04/G04 continuous send and commands | Count=0 is truly continuous, stoppable, and stops on disconnect/profile change; loading saved periodic parameters remains inert. Legacy commands default safely; invalid intervals/counts are rejected. |
| Sequence ratio | Use `sequenceMissing / sequenceExpected` only for a nonzero denominator; expected means finalized present+missing positions, excluding pending window positions/duplicates. Verify before/after stop, reordering, multiple sources, reset/profile change, modulo wrap and ambiguous half-range restart. Window=1 with 100/102 has one missing of two finalized positions while live, and one of three after finalization; the UI must label that finalized scope. |
| A05 catalog refresh | Actual async directory rescan reflects additions, deletions, and modified sidecars; latest requested directory wins; a stale snapshot cannot overwrite active/finalizing writer metadata; 1024 bound preserves active file; selection follows existing path. Polling/repainting alone cannot count as refresh. |
| G03 capture metadata | Actual protocol summary, formatted start timestamp, bounded sidecar/index status, unknown legacy metadata, current recording, complete/error finalization, and mixed transport summary if applicable. Per-record transport does not alone satisfy file-level metadata. |
| A06 preferences | Auto-scroll, display format, speed mode, recording queue budget, periodic interval/count restore; connection/recording/periodic jobs remain stopped. |
| G01/G02 diagnostics | TX rate is visible; truncation is distinct from application queue drops/display omission/sequence gaps; structured events increment statistics without RX payload counters or terminal state. |
| Async storage/sample export | Filesystem traversal and byte conversion/write occur off GUI thread; immutable sample snapshots own shared payloads and export filter scope; bounded one-worker/coalesced storage requests; cancel before commit; stale results cannot update changed path/profile; detached worker owns no QWidget/model/controller references. |

The coordinator has the findings and the exact network evidence. Formal independent review of completed session/UI repairs and broad acceptance is deliberately deferred to the next supervised dispatch.
