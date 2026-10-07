# Audit repair: session backend and shared contracts

Implemented A01/A07, A05 and the session/shared-contract portions of G02–G04. Ownership was `src/session/**`, `include/portbridge/types.hpp`, `include/portbridge/session_controller.hpp`, `tests/test_session.cpp` and this report; UI, network, CMake and packaging changes belong to their respective owners. The original [design/function audit](design-function-audit.md) remains intact.

## Configuration selection and isolation

`SessionController::selectConfiguration(config)` stops connection, periodic sending and capture admission, invalidates the immutable callback generation, selects the supplied configuration without connecting, and clears current error/mailbox, statistics/rate baselines, sequence windows and display state. It emits state/statistics/display/periodic notifications; stop also emits updated finalized sequence statistics. The lifetime accepted-record ordinal and public TCP identity allocator are not reset. Ordinary `stop()` retains disconnected-session evidence, including SYSTEM stop records, finalized sequence counters and prior errors.

Old network/serial callback tokens cannot deliver events or payloads into the new scope. The GUI pump also checks its generation after outward notifications, so a reentrant selection cannot emit a remaining old error mailbox. Disconnected snapshots omit retired-engine pending-send and actual-buffer values. Recorder queue entries carry their statistics epoch; successful late flushes still update their durable capture but cannot update reset statistics. Selection also advances a distinct writer-failure epoch and hides retiring queue/high-water values. A late footer/sidecar failure stays attached to the old incomplete capture, including binary-footer repair, and cannot reappear when the next capture starts. An ordinary statistics reset during recording keeps recording running and counts only new admissions after the reset.

## Asynchronous capture refresh and metadata

`SessionController::refreshCaptures(directory)` submits a directory request to one persistent catalog scanner. Initial load, recording-directory change and manual refresh use the same worker and generation. Requests coalesce into one latest pending directory; obsolete scans check cancellation between entries/metadata reads and cannot publish. Empty directory clears the scoped catalog, except a writer-owned active entry.

The scan streams file names into a bounded latest-1,024 selection. Sidecar reads are capped at 65,537 bytes with over-budget rejection; even a sidecar grown between `size()` and `read()` cannot cause an unbounded read. Missing or invalid metadata reads only the fixed 24-byte header and 32-byte tail of the raw capture. Listing never decodes or reads all raw payload. Publication replaces the directory snapshot to reconcile additions/deletions, while preserving the active path and entries changed by the writer since the scan began. This protects initial-load races, active capture admission, close/rotation publication, and stale incomplete sidecars. The final catalog remains bounded to 1,024 entries including the active entry; files are never removed, and an older file remains available through explicit `exportCapture(path, ...)`.

Scanner shutdown uses the existing three-second recorder owner wait, shares worker-owned state on delayed OS I/O, cancels pending requests and launches no extra scanner per refresh. Writer/scanner startup `std::system_error` is handled as an observable failure instead of leaving shutdown waiting for an unstarted worker. As before, an OS filesystem operation cannot be forcibly interrupted; the owner wait is finite and delayed worker state remains independent of the controller.

`CaptureInfo` appends `transportSummary` and `metadataAvailable`. Transport summary is `SERIAL`, `TCP CLIENT`, `TCP SERVER`, `UDP`, `MIXED`, or empty/unknown for an empty or older capture. The writer accumulates actual record transports per rotated file and atomically persists the summary in the existing schema-v1 sidecar on close. `metadataAvailable` means a successfully initialized or validated metadata sidecar, not an invented full payload index. The new summary is optional when reading older sidecars; missing sidecars preserve bounded header/footer count discovery and report incomplete/unknown catalog status. Numeric metadata strings require successful uint64 conversion, and a supplied summary must belong to the defined set. Completeness is published only after the binary footer/flush and atomic sidecar commit, as before.

## Commands, sequence denominator and truncation

`Command` appends `periodic=false`, `intervalMs=1000`, and `count=10`; `count=0` means continuous. Schema-v1 import accepts absent optional fields independently, retains the old defaults, rejects unknown fields or wrong types/fractions, validates interval 1–86,400,000 ms and count 0–1,000,000, and preserves the prior destination on rejection. Save/export includes all three fields and preserves the prior file on invalid input. Loading commands does not start a connection or periodic task. The existing admitted periodic-request counter now saturates at `INT_MAX` for continuous sending rather than invoking signed overflow.

`Statistics::sequenceExpected` counts finalized sequence positions: present plus missing after an observation window advances, is finalized on disconnect/stop, is evicted for peer/memory bounds, or ends at an ambiguous half-range epoch. Pending positions and duplicate observations contribute nothing until finalization. Each advance adds its finalized span; finish adds the inclusive remaining baseline-to-highest span. Expected and missing counters saturate at uint64 maximum. The displayed ratio contract is `sequenceMissing / sequenceExpected` only when `sequenceExpected > 0`; zero denominator is unknown, not 0%. At saturation both counters remain bounded and no longer represent an exact unbounded lifetime total. Sequence analysis remains opt-in UDP uint64 analysis with existing modular wrap, reorder and bounded-memory rules.

`EventKind::ReceiveTruncated` is appended without renumbering existing events. The controller treats each accepted event as one observable nonterminal error, increments only `receiveTruncatedDatagrams`, and serializes it as a `Direction::System` record with event name `ReceiveTruncated`. It does not inflate RX bytes/datagrams or application-drop totals and does not disconnect a healthy socket. Network classification/emission and UI presentation are outside this owner's changes.

## Verification

Separate owner build: `build/audit-session`, Release, Qt 6.8.3 / MinGW GCC 13.1.0 / Ninja, Windows 11, local `.deps/qtserialport-install` module. Final session compilation emitted no warnings.

```text
cmake -S . -B build/audit-session -G Ninja
  -DCMAKE_MAKE_PROGRAM=C:/Qt/Tools/Ninja/ninja.exe
  -DCMAKE_CXX_COMPILER=C:/Qt/Tools/mingw1310_64/bin/g++.exe
  -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/mingw_64
  -DQt6SerialPort_DIR=C:/Users/threeTeeth/workSpace/github/PortBridge/.deps/qtserialport-install/lib/cmake/Qt6SerialPort
  -DCMAKE_BUILD_TYPE=Release
cmake --build build/audit-session --target test_session -j 4
ctest --test-dir build/audit-session -R '^session$' --output-on-failure
```

Runtime PATH included Qt, MinGW and SerialPort `bin` directories. Final CTest: **1/1 passed, 42.03 seconds** (42.05 seconds total). The final [Qt Test log](../../build/audit-session/test-reports/session.txt) records **26 passed, 0 failed, 0 skipped, 41,848 ms**: 24 substantive slots plus initialization/cleanup. A direct complete run before the final bounded-read refinement also passed **26/26 in 39,576 ms**; the focused seven new slots plus initialization/cleanup passed **9/9 in 2,308 ms**, recorded in [focused-test.txt](../../build/audit-session/focused-test.txt). One preliminary combined build-and-test command hit the tool's 60-second limit during the existing sequence-source test; it produced no assertion failure and was followed by these completed runs.

Seven new substantive regression slots:

| Test | Evidence |
| --- | --- |
| `commandPeriodicCompatibilityAndValidation` | Old defaults, independently optional new fields, continuous round-trip/save, invalid bool/type/range/fraction/unknown-field rejection, destination/file preservation, no automatic connection or task. |
| `offlineSelectionIsolatesCallbacksAndWriter` | Existing UDP loss/error plus active recording/periodic work, switch to disconnected TCP, stale/current-offline callback rejection, zero current errors/counters/queues/buffers, completed old capture, lifetime ordinal continuation, ordinary-stop evidence retained. |
| `offlineSelectionFencesLateWriterFailureAcrossNextCapture` | Real sidecar path replaced by a directory before selection triggers a late atomic close failure; old catalog/footer remains incomplete while the next capture has clean error/failure/count/high-water state. |
| `catalogRefreshReconcilesAndProtectsActiveCapture` | Copied valid capture additions, deletion reconciliation, repeated latest-directory requests and startup overlap, active capture preserved across a different-directory scan, accurate finalized writer metadata, manual export, empty directory and finite shutdown after 1,000 refreshes. |
| `captureMetadataCompatibilityAndTransportSummary` | Durable MIXED and each of the four transport summaries, old sidecar without summary, invalid summary/numeric string, absent sidecar, bounded fallback counts and raw export without metadata. |
| `finalizedSequenceDenominatorAndSaturation` | Pending denominator 0, duplicate exclusion, reorder/window advance, stop inclusive finalization, uint64 wrap, half-range epoch, counter saturation, reset, 65-source eviction (195 expected/65 missing after finalization). |
| `truncationIsObservableNonterminalAndScoped` | Production controller callback seam: one nonterminal error/counter/SYSTEM record, no RX/drop inflation, next RX accepted, durable export, stale event rejected after selection. |

The existing 17 substantive tests remain green, including 1,028 rotations retaining 1,024 catalog entries/all files, manual old-file export and asynchronous rescan, complete-capture publication observer, strict decoder/cancellation and prior-output preservation, TCP raw reconstruction/backpressure/stable client identities, bounded paused/high-speed display, recording queue capacity accounting, SYSTEM traffic exclusion, codecs/configuration and serial open errors.

## Limits and remaining integration

The session truncation regression injects a typed event through a narrowly scoped internal friend helper which calls the production event/generation gate. It does not induce a physical oversized UDP receive or reduce the real receive buffer. The network owner's separate completion-classification regression and UI owner's error/ratio/catalog/command presentation must be considered with this report for integrated acceptance.

Thread-creation exhaustion and permanently stalled OS I/O were reviewed in code rather than fault-injected. Local finite-shutdown/repeated-refresh checks do not establish network-share latency guarantees. Physical serial hardware, two-machine 2.5G sustained throughput, sustained disk saturation, clean-system packaging and UI design comparison remain external acceptance work; this owner did not edit or certify those areas.

## Coordinator-authorized follow-up repairs by the independent network/session reviewer

After the original session owner settled, the coordinator authorized two narrow follow-up changes by the worker in terminal `term_a3199e79-9f9a-453a-8d85-a9ae9de8ea46`. These two repairs are reviewer-authored, so their independent verification belongs to the coordinator; the reviewer's testing of the remaining session implementation remains independent.

- `Recorder::State::loadCatalog` clears all parsed numeric fields when sidecar validation fails, then repopulates only the bounded raw header/footer facts. A rejected sidecar cannot retain a fabricated duration; duration stays zero/unknown. The permanent metadata test first loads a valid 999-second duration, then rejects malformed numeric metadata and verifies zero duration plus the actual raw start/record/byte values.
- `SessionController::start` captures its session generation and rechecks it plus `wanted` after the synchronous Connecting `stateChanged` notification. A subscriber's nested selection/stop/start supersedes the original request, and the outer request cannot start an invisible old socket or start its replacement engine with old configuration. A permanent regression verifies offline selection and nested replacement start using real UDP bindings and port reuse.

Follow-up isolated verification is recorded in [the independent session report](audit-independent-session.md); original owner validation above describes the pre-follow-up source and is retained as provenance. The coordinator independently reviewed and reran the unchanged metadata/reentrant-start reproducers successfully in `docs/validation/audit-fix/coordinator-session-reproducers.txt`.

A subsequent coordinator root run exposed fixture send readiness in `sequenceWrapAndSourceEviction`: UDP Bound precedes target resolution. Under narrow authorization the reviewer latched/retried only the initial wrap datagram's admission, preventing duplicate QTRY sends while preserving the exact four-RX/missing assertions. This is a test-only readiness fix; production source remains unchanged. The final independent driver then passed all five commands with identical source manifests, including 12/12 independent and 27/27 product session Qt results.
