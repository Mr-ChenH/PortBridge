# Independent data-path review and Windows UDP follow-up

Outcome: the prioritized findings below are resolved in the final reviewed snapshot, and the exact regression checks pass. No additional concrete deadlock, stale-callback, byte-accounting, capture/export, or persistence defect was demonstrated in that snapshot. This is a software functional review on Windows localhost, not physical 2.5G or serial-device certification.

The active review task is `task_a6e1e0d81012`, dispatch `ctx_dbcb79d43d13`. Product changes made by this reviewer are limited to the coordinator's explicit ownership extension: `src/network/network_engine.cpp` and `tests/test_network.cpp`, for the Windows UDP absent-peer fix. Session, UI, shared headers, root CMake, and scripts were read but not edited. This report is the only review document changed; snapshots, verification sources, settings, logs, and build outputs are under `build/data-review`. No agents or Git commits were created.

## Prioritized findings and disposition

| Priority | Finding / affected function | Concrete evidence before correction | Final disposition |
| --- | --- | --- | --- |
| P1 | Premature capture completeness: `detail::Recorder::State::run`, `finalize` lambda | `update()` published `CaptureInfo.complete=true` before `writeCatalog(current)` committed the final metadata. A real Windows observer saw a complete in-memory snapshot while the existing sidecar still said `complete=false`. Reading that prematurely advertised sidecar also collided with Windows file replacement: of 100 admitted records, 1 was recorded and the recorder reported 1 failure. | Session owner moved complete publication after footer write/flush and successful sidecar commit. Exact observer rerun: premature publication 0, admitted 100, recorded 100, failures 0. Failure/repair publication stays incomplete. |
| P1 | An absent UDP destination destroyed the independent local binding: `NetworkEngine::Impl::readUdp` | Coordinator reproduced Windows error 10061 through the outstanding UDP receive; the generic `fail(run)` path closed the bound socket and could suppress a pending completed TX callback. | This reviewer implemented nonterminal classification of connection-refused/reset, host-unreachable, network-unreachable, and network-reset notifications. The error remains visible with its operation/code and configured peer/local endpoints; receive is reposted on the same socket. Fatal descriptor/bind failures remain terminal. A deterministic Windows regression observes the actual ICMP error, completes 64-byte TX, receives an actual datagram from another source at the original local port, completes another 13-byte TX, and leaves no pending send reservation. |
| P1 | Unbounded capture catalog: recorder `open`, `loadCatalog`, and `captures_` | Initial source appended every rotated capture and eagerly loaded every sidecar, with no in-memory catalog bound independent of queue budget. A valid 128-byte rotation threshold can create approximately one catalog entry per tiny record indefinitely. | Session owner added a 1,024-entry recent catalog, protects the active path, streams directory entries through `QDirIterator`, and bounds the preloaded candidate map before reading sidecars. Existing regression creates 1,028 captures, retains exactly 1,024 catalog entries, keeps all raw files, exports an older file, and reloads a bounded catalog. |
| P2 | Modular sequence wrap was ignored: `SessionController::Impl::analyze` | Four real localhost UDP datagrams with sequences `[UINT64_MAX-1, UINT64_MAX, 0, 2]` produced missing 0 and reordered 2; sequence 1 is absent and the transition is a forward wrap. | Epoch/value tracking and modulo half-range ordering now produce missing 1 and reordered 0. The half-range ambiguous jump has an explicit reset/error policy, and missing-count accumulation saturates instead of overflowing. |
| P2 | Source eviction discarded pending gaps: `SessionController::Impl::analyze` | 65 actual source sockets each sent `[100,102]`. RX correctly counted 130 datagrams, but stop finalized only 64 missing sequences because the evicted window was erased first. | `finishSequence` now runs before both peer-limit and memory-limit eviction. The exact rerun reports 65 missing sequences and 130 received datagrams. Eviction remains visible as an analysis-limit event; observations outside retained windows do not imply unlimited duplicate tracking. |

All findings were sent to the coordinator as they became actionable, then rechecked after the session owner's stable checkpoint. A transient syntax/header mismatch captured during active owner edits was excluded from findings; it was not used as evidence of a final build failure.

Windows case-alias export was also checked with a real finalized file: export to the uppercase spelling of the same capture path returns false with `Capture and output must be different files`, and the original raw bytes remain identical. This is a verified guard, not an open finding.

## Final focused results

`build/data-review/final-focused-results.txt` contains:

```text
CATALOG records=200 entries=200 queue_budget=1048576 per_entry_bytes=104
WRAP transmitted=[MAX-1,MAX,0,2] expected_missing=1 actual_missing=1 expected_reordered=0 actual_reordered=0
EVICTION sources=65 expected_missing=65 actual_missing=65 rx_datagrams=130
CASE_ALIAS exported=0 source_preserved=1 error=Capture and output must be different files
COMPLETE_PUBLICATION saw_complete_snapshot_with_incomplete_durable_catalog=0 admitted=100 recorded=100 failures=0
Focused review completed; reproducer output reports behavior, not performance.
```

The publication observer polls the recorder's public catalog and reads actual sidecar files only for entries already advertised complete. It uses real file operations, not an artificial sleep inside product code. An earlier version of this temporary reproducer terminated while unwinding past a joinable observer thread after recorder failure; the harness was corrected to always stop/join its observer and print failure counters. That harness termination was excluded from product findings; the corrected before-fix run separately demonstrated the premature flag and actual recorder failure.

The UDP source sends in the focused test occur exactly once before waiting on pure statistics predicates. The new network test's `until(send)` stops immediately after its first successful admission. Neither uses a side-effecting send inside Qt's multiply evaluated `QTRY_VERIFY` macro.

The before-fix behavior is preserved in `build/data-review/pre-fix-focused-results.txt`; its reviewed session source SHA-256 was `d508434c3964ad8c78a80bb9ae3d395969fb0f14ee2742342885b1945e6f305a`, and recorder source SHA-256 was `cb3203aa7bf671c0f8653fa9c980356335d5b6b7695e196f7a257bba36624566`. Those hashes identify the snapshot used for the demonstrated wrap, source-eviction, and publication failures, rather than later live edits.

## Lock, lifecycle, and accounting review

The following observations combine source inspection with the functional tests; they are not a mathematical proof over every possible OS failure.

- Network/serial callbacks enter the shared callback gate, then the session mutex, then recorder admission. Recorder workers do not acquire the session mutex or call the controller. GUI stop releases the session mutex before engine cleanup; serial send/close/written release their admission mutex before invoking callbacks. No reverse lock order was found in these paths. Qt state/data/error signals are emitted from the timer poll outside the session mutex.
- `recordEvent` calls `store` while already holding the session mutex; it does not recursively call the locking `ingest` entry point. SYSTEM records preserve control events and local endpoint details without increasing RX/TX traffic counters or requesting TCP/serial retries. The final regression verifies their offline reconstruction and exclusion from traffic statistics.
- Start/stop invalidates session generation before old engines are retired. Retired callbacks are gated by generation and by the shared alive gate; a detached cleanup worker owns its engines/state and cannot dereference the destroyed controller once the gate closes. Retirement admission is bounded to four pending cleanup batches, and a denied restart does not create another engine pair. The TCP replacement regression sends through the old peer during replacement, then observes zero old RX in the new generation and a newer public client ID.
- `store` returns false on retryable recorder saturation before advancing the local ordinal, RX bytes, sequence analysis, or display samples. Network TCP retains the same block and stops further reads while retrying. The 70,000-byte recording budget test reconstructs a 1,048,580-byte TCP payload exactly, with stable client IDs, no application drops or recording failures, and queue high-water within budget. A block that can never fit ends recording with an explicit incomplete error instead of creating permanent TCP backpressure. Completed TX consumer rejection never causes resending.
- UDP receive byte/datagram statistics count actual application-delivered datagrams. Recorder overflow increments recording failures and the documented application recording-queue drop counters, while UDP receive continues. Display omission is separate. These counters do not measure NIC/kernel loss, and a successfully completed UDP TX does not claim peer receipt. The Windows absent-peer notification is now nonterminal at the engine level; session Error events leave the bound state active unless a terminal disconnect is reported.
- Receive/display/recording admission includes payload retained capacity and record/address metadata. Display bounds are 10 MiB/50,000 records normally and 2 MiB/2,000 in high-speed mode, with finite drain batches. Recording has a byte budget and 65,536-descriptor limit; the writer's separate bounded batch/serialization working memory is additional to the queued budget. Sequence state is bounded to 64 peers and 1,048,576 retained positions. Network send admission charges retained payload capacity and has a descriptor limit; empty UDP cannot create unlimited zero-cost queued work.
- Serial configuration/open and fatal errors execute on the serial thread. `NoError` and `NotOpenError` are ignored as appropriate; fatal errors close the port and clear pending/held state. The latest initial-close path suppresses a spurious pre-Connecting Disconnected event. The missing-device test uses the real QSerialPort OS open path and verifies that connecting ends, connected remains false, the error is observable, and periodic sends cannot start. Successful hardware open, hot unplug, flow control, and device TX completion remain unverified.
- Recorder directory creation, catalog scanning/open, and file writes now run in background work; initial catalog loading is asynchronous and bounded. Stop refuses new admission while queued records finalize. A new recording cannot start while the preceding writer is still finishing. Normal close/rotation produces a validated DONE footer and catalog, and raw rotation files are retained even when old in-memory entries are evicted.
- `exportCapture` validates header, lengths, enum/reserved fields, UTF-8 peer text, payload bounds, footer totals, and finalization. `QSaveFile` preserves prior output on invalid input or cancellation. Progress is monotonic and bounded by source size; false or an exception cancels before commit. Case/canonical alias checks protect the input. These checks validate format and byte reconstruction; the format does not claim cryptographic integrity or power-loss durability.
- Profile/command import validates schema, fields/types/ranges and candidate data before replacing the caller's collection. JSON writes use `QSaveFile`; invalid commands and excessive aggregate size leave the prior file untouched. Tests cover UTF-8 Chinese values/paths, round-trip sequence settings, corrupt JSON, unknown schema, wrong types, and the 16-MiB limit. Loading config does not auto-connect or send.
- Root CMake uses ordinary console network tests and Qt tests through `cmake/RunQtTest.cmake`. The wrapper propagates nonzero exit or process timeout, and preserves readable test output. Build/test PowerShell scripts add the matching MinGW, Qt and SerialPort runtime paths and propagate native failures. The build script passes an explicit matching compiler and `Qt6SerialPort_DIR`; review did not execute its root full-build path concurrently with owners.

## Builds and tests

All review builds used frozen copied sources in `build/data-review/snapshot`, Qt 6.8.3, and `C:/Qt/Tools/mingw1310_64/bin/g++.exe` (GCC 13.1.0) with `C:/Qt/Tools/Ninja/ninja.exe`. The temporary CMake project builds only the network/session libraries and review tests; it does not build the root app/UI or modify coordinator build state.

```sh
python build/data-review/snapshot.py
cmake -S build/data-review -B build/data-review/out -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_COMPILER=C:/Qt/Tools/mingw1310_64/bin/g++.exe \
  -DCMAKE_MAKE_PROGRAM=C:/Qt/Tools/Ninja/ninja.exe \
  -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/mingw_64 \
  -DQt6SerialPort_DIR=C:/Users/threeTeeth/workSpace/github/PortBridge/.deps/qtserialport-install/lib/cmake/Qt6SerialPort
cmake --build build/data-review/out --parallel 3
PATH='/c/Qt/Tools/mingw1310_64/bin:/c/Qt/6.8.3/mingw_64/bin:/c/Users/threeTeeth/workSpace/github/PortBridge/.deps/qtserialport-install/bin':"$PATH" \
  build/data-review/out/focused_review.exe
PATH='/c/Qt/Tools/mingw1310_64/bin:/c/Qt/6.8.3/mingw_64/bin:/c/Users/threeTeeth/workSpace/github/PortBridge/.deps/qtserialport-install/bin':"$PATH" \
  ctest --test-dir build/data-review/out --output-on-failure
```

Final results:

- `network_snapshot`: passed, 1.50 seconds in the final focused rerun; seven console groups including the real Windows absent-peer/independent-origin/TX test. Its source socket is reserved before releasing the absent destination port, guaranteeing a distinct endpoint even if ephemeral ports recycle.
- `session_snapshot`: passed, 39.30 seconds; 19 Qt Test entries, 0 failed/skipped, including modular wrap/source eviction, catalog bounding/reload, completeness observation, TCP capture backpressure/replacement IDs, SYSTEM records, cancellation/corrupt capture export, persistence, display accounting, periodic stop, and real missing-device serial open.
- Combined CTest run: 2/2 executables passed, 40.77 seconds total. A subsequent network-only rerun passed after strengthening the test fixture's distinct-source port reservation; session/library sources remained identical. These durations describe functional tests, not software performance thresholds.
- Frozen build log contains no network/session review target warnings or compile errors.
- `build/data-review/live-hash-comparison.json` reports no live reviewed file changed after the final freeze/test run.

Evidence files: `final-build-log.txt`, `final-ctest-log.txt`, `final-network-ctest-log.txt`, `session-test-results.txt`, `final-focused-results.txt`, `pre-fix-focused-results.txt`, and `reviewed-hashes.json` under `build/data-review`.

## Final reviewed source SHA-256

The manifest below identifies the exact compiled/reviewed snapshot. Dependencies and test files are included so a later source change cannot be mistaken for these results.

| File | SHA-256 |
| --- | --- |
| `src/session/configuration.cpp` | `850b315ad9cfd85631626748116472cc335dc845a88abfd1b048560776ae76ea` |
| `src/session/recorder.cpp` | `327b7aa663f0797d9665afee7c896aba9d6cbe17b00ecacdbf819152fdf9e1e0` |
| `src/session/recorder.hpp` | `76674d3a763b7e5ac9e108f954145ff0bd75ac6a05f02f20175c6bdb4c1d4da0` |
| `src/session/serial_engine.cpp` | `3f68e5fe06e8c69344eb1b339dff2439feb6f18194385653c898e3a53813b54b` |
| `src/session/serial_engine.hpp` | `305d2a8b3a6499bdf1c51c71a75c50f4fb7073d7ec2648df095df9fbd541413d` |
| `src/session/session_controller.cpp` | `b31f92b233cae68526f0778a9fca2d4e65ce925edad0ccd014c10495040bfd07` |
| `src/session/session_private.hpp` | `83834e9467c7efd392c3c3fa659545aad56886b9b6b7277ec8dcb60cd07ce790` |
| `include/portbridge/network_engine.hpp` | `1428c28956f023118a8ee0944fee10a7cd2d8f6de3799a1a454ba37ace5fe6b3` |
| `include/portbridge/session_controller.hpp` | `39acd3d9cce545846140a6d2b509b6b47b942c47d7380b1a13d424e62df68b28` |
| `include/portbridge/types.hpp` | `714265b99e99b444e102543d366beac532279a67636af8229b90cfac52435d0d` |
| `src/network/network_engine.cpp` | `389193183c5448a673639f1992b90089e14437fbb9cea02ba6c42d27b14b7fd3` |
| `tests/test_network.cpp` | `77b05b96645e3a86090a19ac5f47487f0862b11a4ebe68c038d2d445e4965163` |
| `tests/test_session.cpp` | `b28f1a9ce1f2d4c85c4c0d9d4aea6fb10ca21692a2bda32a78ae99b26331b977` |
| `CMakeLists.txt` | `22bcd30ba3d3588f9e30fda84f5dcdb671f7d5e8ee33de5bf276053342f04383` |
| `scripts/build.ps1` | `17510417d650d3061ee37dc5b85874d6459b264d86662497bd1ba3a362a2dda7` |
| `scripts/deploy.ps1` | `bf8a728e9ffd20cc91578a85e09e8e467669a54434f7b43be82cfb414d8c29b8` |
| `scripts/test.ps1` | `28c3e83f196329e17d48bcf28f5a1e80acf1be5df7c5959ef8bd31c60e039191` |
| `cmake/RunQtTest.cmake` | `127e64c0265fdf8b6ad6017e79811f78b9488ba903fcd5c99200e68c96150b66` |

## Limits and unproved exit conditions

Real serial successful open, loopback, hot unplug, sustained baud rates, and hardware flow control were not available. Two-machine physical 2.5G throughput/PPS/burst behavior, NIC/kernel drops, simultaneous high-load capture/UI conditions, sustained disk saturation, power-loss behavior, and clean-system deployment were not measured. No result from a mock is presented as a performance claim; the focused regressions used actual localhost UDP sockets and real Windows files.

Normal background retirement and capture drain are covered by the tests, but pathological OS DNS/file I/O blocking is not. The current recorder waits up to three seconds at destruction; on timeout it marks shared state incomplete and leaves a worker owning that state, while the initial on-disk catalog stays incomplete until successful finalization. Engine cleanup similarly uses a guarded shared cleanup state if cancellation outlives its three-second destructor wait. These mechanisms protect controller lifetime and avoid false initial completeness, but this review does not certify that an OS-blocked worker has actually terminated before process exit, nor that a timeout reason can always be persisted to a blocked filesystem. Those are explicit remaining fault-injection/OS exit-validation limits, not a claim that bounded waiting cancels an arbitrary OS operation.
