# Independent session audit repair review and benchmark G02 repair

Date: 2026-10-07. **Final session verification passed:** the independent executable passed 12 Qt results (ten business probes) and the real product session suite passed 27 Qt results, with reviewed source hashes unchanged across the final run. The two identified metadata/startup issues were repaired under explicit coordinator authorization after the original session owner settled; these reviewer-authored changes are distinguished from independent review of the remaining implementation. The coordinator subsequently reviewed the final patches and independently reran the unchanged two reproducers successfully (`docs/validation/audit-fix/coordinator-session-reproducers.txt`).

## Scope and independence

This dispatched follow-up reviews session repair implementation owned by another worker. The initial scope owned only `tools/network_bench.cpp`, the independent harness under `build/audit-independent/`, and this report. After the session owner settled, the coordinator explicitly expanded authorization to the exact invalid-sidecar fallback in `src/session/recorder.cpp`, the start-generation/wanted guard in `src/session/session_controller.cpp`, their permanent assertions in `tests/test_session.cpp`, and a follow-up section in `docs/reviews/audit-fix-session.md`. Those two fixes and their owner-test changes were authored by this reviewer; all other session/shared-contract implementation review remains independent. It does not change UI code, shared contracts, root CMake, release outputs, or deployment packages. No agents were created.

Read the original design/function audit, previous network reviews, the real session configuration/controller/recorder/serial sources and headers, owner tests, and the in-progress UI consumers of the shared metadata/statistics contracts. Independent test code calls the public APIs and the session owner's real callback seam; it does not copy or replace the session implementation.

## Benchmark G02 repair

The benchmark now treats structured `ReceiveTruncated` as a transport observation requiring explicit accounting:

- `receive_truncated_datagrams`: total observed truncation events, including teardown.
- `teardown_receive_truncated_datagrams`: the teardown subset of that total.
- `transport_errors`, `teardown_errors`: keep their previous aggregate meaning, which included truncation when it was a generic Error event. Runtime and teardown truncation remain included in the respective original totals.
- Runtime truncation records the first error, marks the run fatal, and makes `success:false` / exit 2 through the existing consistency check. The NetworkEngine itself remains bound/nonterminal; benchmark measurement validity is a separate decision.
- Teardown truncation preserves the original teardown-error policy: visible in teardown totals, without converting an otherwise completed run into a runtime failure.

Normal CLI outputs and existing fields remain compatible; the two named counters are additive fields. Shared `Metrics::transportEvent` handles the actual event callbacks and the review seam.

The isolated `bench_classification.cpp` includes the actual benchmark implementation with its CLI main renamed. It calls the actual private UDP completion helper with simulated `message_size`, positive partial length, and explicit endpoints, forwarding the resulting structured event into actual benchmark Metrics accounting. It verifies no partial payload admission/RX counters, one continuation, zero fatal transport callbacks, one benchmark runtime error/truncation, disabled success eligibility, first-error text, and separate teardown classification. This is truthful **classification simulation**, not an OS/physical truncation or entire benchmark-run event injection. No production CLI injection flag or reduced receive buffer was added.

Final genuine TCP/UDP self-loop cases each completed and received 200 frames, using 4096-byte TCP / 1472-byte UDP payloads at requested 1000 frames/s for 0.2 seconds. Actual completed TX, unique RX, admission counts, expected counts and bytes reconcile: TCP 819200 bytes and UDP 294400 bytes. Both new counters, transport errors, invalid frames and pending-at-stop are zero; both runs drain and exit 0. The UDP case has 330 rejected admission attempts retried before admission (destination readiness/queue timing), not 330 lost completed datagrams. Short localhost runs are functional evidence only.

## Independent probes

The independent Qt executable has ten business cases, with real local filesystem/socket behavior except explicitly identified callback injection:

| Probe | What it establishes |
| --- | --- |
| Selected profile drops retired callbacks/counters | Real UDP session and queued actual recorder work; inject old Error/ReceiveTruncated/Bound/Connected/ClientAdded/SendRejected/data through production token gate; select offline TCP server; ensure no late state, errors, traffic, recorder/sequence/truncation counters, samples, clients, or active recording. |
| Reentrant selection during start | A direct Qt stateChanged subscriber selects offline TCP while original UDP start reports connecting; an actual exclusive UDP bind on the original port proves no hidden old socket remains. |
| Reentrant profile change stops queued errors | Queue three errors through production callback dispatch; first emitted error changes profile; ensure later old errors do not cross selection or repopulate lastError/samples. |
| Old finalization error never reappears | Replace the initialized sidecar with a directory, isolate statistics before stopping an idle writer, force a real QSaveFile finalization failure, then start recording anew; old failure must remain in old capture metadata without new-profile failure totals. |
| Reset during writer flush | Enqueue 100 x 65536-byte pre-reset records, reset, enqueue five four-byte post-reset records; durable capture keeps 105 records, while statistics show only five records / 20 bytes. |
| Independent sequence model | Compare each of 1000 deterministic gap/duplicate/reorder/late arrivals to an independently maintained set and finalized boundary; compare expected/missing after every arrival, stop and second stop, then reset. |
| Wrap, half-range and source eviction | Verify modular uint64 wrap with within-window reorder/gap, ambiguous half-range restart/finalized denominator, and 65 sources over the 64-source cap: 195 finalized positions / 65 missing after stop. |
| Latest catalog, deletion and modifications | Alternate 200 requests between actual directories; latest wins; added/deleted raw files and changed valid sidecar summary/duration appear; empty-directory request clears inactive catalog. |
| Active/finalizing refresh race and bound | Populate 1030 actual valid raw files/sidecars; scan capped at 1024; record/rotate 50 more while independent thread refreshes and observes the bound; final metadata remains complete/durable, UDP summary retained, all 1080 raw files remain on disk. |
| Legacy/invalid command and capture metadata | Legacy command defaults, modern continuous parameters, invalid types/ranges/unknown fields with destination preserved; old sidecar summary unknown, malformed numeric sidecar incomplete/unknown, no fake duration retained; raw file remains manually exportable without sidecar. |

These probes additionally cover original audit concerns through independently chosen inputs; the product session suite remains necessary for serial-error/recording/export/backpressure and other existing behavior.

## Findings raised before final readiness

### SESSION-REVIEW-1: invalid sidecar leaked fabricated duration

Reproduction: valid empty raw capture with a sidecar containing `startedUs:"not-a-number"` and `durationUs:"999000000"`. The loader rejected metadata (`metadataAvailable=false`, `complete=false`) but retained 999 seconds in CaptureInfo, which the UI could display. `build/audit-independent/probes-added-preliminary.txt` records actual 999000000 versus expected unknown zero. Source cause: populate numeric fields before complete validation, then leave duration unreset in raw fallback. **Resolved by the coordinator-authorized reviewer patch:** all untrusted numeric fields are cleared before raw fallback. The unchanged independent assertion and strengthened permanent owner assertion both pass on final source; the coordinator also independently reran the unchanged reproducer successfully.

### SESSION-REVIEW-2: profile selected synchronously during start left invisible UDP binding

A direct Qt `stateChanged` subscriber called selectConfiguration(TcpServer) as soon as the original UDP `start` set connecting. After that selection the controller reported disconnected/not connecting, but the original `start` resumed after signal emission and started the old UDP engine. An actual exclusive bind failed with AddressInUse. `build/audit-independent/probe-start-reentrant.txt` records the failure. **Resolved by the coordinator-authorized reviewer patch:** capture the request's session generation and check it plus `wanted` after the synchronous startup signal before calling either engine. The unchanged exclusive-bind reproducer and new permanent offline-selection/nested-start regression pass on final source; the coordinator also independently reran the unchanged reproducer successfully.

### Earlier source observations, independently tested

The prior late-writer failure source concern is now addressed by a separate writer `failureEpoch`: profile isolation advances the epoch; old file finalization remains visible in its metadata while its error cannot modify new-profile statistics. The deliberately blocked real-finalization probe passed on final source. Periodic sent now saturates at INT_MAX, avoiding signed overflow for continuous mode. Catalog scanner construction failure is caught and surfaced as recording/scanner failure rather than escaping construction. Numeric/transport-summary sidecar validation rejects malformed values, with the separate duration fallback finding above.

## Build and evidence

Toolchain: Windows localhost, Qt 6.8.3, MinGW 13.1, Ninja, project-local Qt SerialPort. Harness uses `add_subdirectory(../.. product EXCLUDE_FROM_ALL)` and real product targets, preserving shared/release build directories.

```sh
cmake -S build/audit-independent -B build/audit-independent/out -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_COMPILER=C:/Qt/Tools/mingw1310_64/bin/g++.exe \
  -DCMAKE_MAKE_PROGRAM=C:/Qt/Tools/Ninja/ninja.exe \
  '-DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/mingw_64;C:/Users/threeTeeth/workSpace/github/PortBridge/.deps/qtserialport-install' \
  -DQt6SerialPort_DIR=C:/Users/threeTeeth/workSpace/github/PortBridge/.deps/qtserialport-install/lib/cmake/Qt6SerialPort
cmake --build build/audit-independent/out \
  --target independent_session independent_bench_classification portbridge_bench test_session -j 4
python build/audit-independent/verify.py --final
```

The verification driver sets matching Qt/MinGW/SerialPort runtime PATH, captures exact commands/return codes/stdout/stderr, checks benchmark invariants, and hashes all session/network sources, shared headers, owner tests, benchmark, and independent harness before and after final verification. The session owner's settlement and coordinator notice that remaining session implementation was stable preceded the follow-up fixes; the reviewer then announced their stable source before the final run.

Final evidence:

- `build/audit-independent/probes-final.txt`: **12 passed, 0 failed, 8032 ms**, ten independent business probes plus initialization/cleanup.
- `build/audit-independent/product-session-final.txt`: **27 passed, 0 failed, 39555 ms**, 25 substantive product slots plus initialization/cleanup.
- `build/audit-independent/product-followup-focused.txt`: metadata and reentrant startup permanent regressions **4 passed, 0 failed, 990 ms** including initialization/cleanup.
- `build/audit-independent/results-final.json`: all five commands return 0, actual benchmark JSON/count checks pass, and `source_stable:true`.
- `build/audit-independent/hashes-before-final.json` and `hashes-after-final.json`: identical full source/harness manifests. Earlier successful preliminary logs and the two independently reproduced failure logs remain available.

Key final SHA-256 hashes:

| File | SHA-256 |
| --- | --- |
| `src/session/session_controller.cpp` | `62982a15232ba7b8ebfb7e7b859d93120838bf87d217ffc25dd1467ffa303acb` |
| `src/session/recorder.cpp` | `075c8b225902879c9641a4844ac0082f546765504138f4b4742b2b7acc3f0ed1` |
| `src/session/configuration.cpp` | `2451a922e72fed452f12e8f65c89b2b4aebc8fe7de047f5483049f844bb62422` |
| `include/portbridge/types.hpp` | `365ee06f65fb9a4b6815c4df58c5458acd3bf429747683b99d325dd50be643b7` |
| `include/portbridge/session_controller.hpp` | `17086c3f4fc2fc694c2b39c04dd35b0e68c87bc636a691c9a82cd8b7a80989d8` |
| `tests/test_session.cpp` | `986db84afcd42acdedc9bd8056d8b051e46f92e5e80bdd6fea780c90c9f2deb5` |
| `tools/network_bench.cpp` | `f19c0cf3e972b8ff817d4a73afe2f36f87c463d4d1560097e14c4a2b868d01fb` |

No further concrete session blocker was identified within the reviewed/tested scope. This is a scoped finding with the limitations below, not proof that every possible Qt reentrant signal order or OS schedule has been exercised.

## Final fixture readiness follow-up

The coordinator's root run reproduced an intermittent test-only race at the first wrap datagram: NetworkEngine's UDP Bound notification precedes target DNS completion/send readiness. Under narrow coordinator authorization, `sequenceWrapAndSourceEviction` now latches and retries only the initial `maximum-1` admission, mirroring the existing 65-source fixture; subsequent `maximum`, `0`, `2` sends and exact four-RX assertions remain unchanged. The latch prevents Qt's final QTRY assertion from enqueueing a duplicate. Production semantics were unchanged, and the full independent/session/benchmark driver was rerun with identical before/after manifests; final counts/timings and test hash above refer to this latest run.

## Limits

Callback-token/reentrancy tests simulate only callback content through the real session handler; they do not claim physical late NIC/serial callbacks. Recorder failure, catalog files, asynchronous scanner/writer concurrency, exclusive binding, and benchmark self-loop traffic use actual OS facilities. The refresh stress observes real races but does not deterministically pause the scanner at every instruction or certify all schedules. No thread-construction/resource-exhaustion injection, OS filesystem call that stalls forever, hardware serial, physical truncation, physical 2.5G/sustained throughput, long-running performance/RSS envelope, UI visual acceptance, or packaged release is verified by this report. Sequence counters saturate at uint64 maximum; saturation is not proof of an exact unsaturated ratio.
