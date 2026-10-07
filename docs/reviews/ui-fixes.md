# PortBridge post-review UI fixes

The four defects reported by the independent reviewer are fixed in the native Widgets implementation. The review reproduction is preserved at [focused-before.txt](../../build/ui-independent/focused-before.txt), with the unchanged focused test source at [focused_ui.cpp](../../build/ui-independent/focused_ui.cpp). Owner verification uses the same Qt 6.8.3 / MinGW 13.1 environment and real loopback/file behavior.

This dispatch changed only `src/ui/main_window.cpp`, `tests/test_ui.cpp`, `docs/reviews/ui-implementation.md` and this report. No shared headers, session/network code, CMake, new features, agents or commits were introduced.

## P1: precise profile values were silently rewritten

The buffer editors previously divided imported values by MiB, used smaller control ranges, and multiplied rounded values on persistence. Timeout and some dormant fields also used ranges narrower than the shared schema. In the review case this changed RX 12,345 B and TX 23,456 B to 1 MiB, reduced a 512 MiB send queue to 256 MiB, and changed a 50 ms timeout to 100 ms on an untouched window close.

RX/TX/send-queue editors now display and persist exact integer bytes over the schema range 1–1,073,741,824 B. Timeout uses 1–600,000 ms. Sequence offset accepts its full 0–67,108,856 range, and a dormant remote port of 0 is preserved for serial/server profiles while an active UDP/TCP-client destination still requires 1–65,535. These changes preserve valid imported values rather than applying silent clamps. Historical `receiveBufferMiB`, `sendBufferMiB` and `sendQueueMiB` objectNames are retained for existing integration/review cases; visible labels and suffixes explicitly say bytes / B.

Initial restoration of a saved nonzero selected profile also blocks the list-selection signal until its configuration is applied. Otherwise the selection handler could overwrite that profile with uninitialized/default editor values. The exact-profile regression checks complete versioned JSON equality across import, selection of all profiles, untouched close, and another untouched open/close at saved index 2. It covers precise values plus schema minimum/maximum values and dormant fields. Separate explicit byte/timeout edits are saved and restored on restart.

## P1: statistics reset incorrectly destroyed UTF-8 continuity

The previous snapshot reset every decoder whenever `displayOmitted` changed, including decreases caused by resetting statistics. That treated a counter operation as a missing byte and converted the final AD byte of E4 B8 AD into a replacement character.

Continuity now follows the SessionController lifetime `DataRecord.sequence` ordinal. It advances for actual admitted RX/TX/SYSTEM records, including records omitted from display, and is not reset by `resetStatistics`. The UI groups each contiguous batch and resets decoder state at the exact boundary of a missing/out-of-order display record. Explicit clear/mode/profile changes establish a new display baseline. Resettable statistics no longer control decoder state or retained display contents.

The real TCP regression first produces an earlier omission, then delivers E4 B8, resets statistics through the UI, and delivers AD. It asserts retained rows/text are unchanged by reset and the result is 中 without U+FFFD. A second phase deliberately omits a stream record using controller pause/resume, resets the omission counter before the next record, and confirms the actual ordinal gap still resets decoding, emits the discontinuity notice and produces U+FFFD for a standalone AD. A subsequent complete UTF-8 character confirms decoding recovers.

## P2: selecting a live server client left Disconnect disabled

The selection signal previously called only send validation. Disconnect state was recalculated only during unrelated state changes, and that calculation could retain a stale ID from before rebuilding the selector.

A target-specific refresh now reads the current selected ID and actual current client set, updates target/Disconnect state, and validates send controls without rebuilding or changing the selector. Selection invokes it immediately. General state refresh rebuilds the selector under QSignalBlocker and then checks the resulting current selection, so a disappeared selected peer leaves placeholder ID 0 and Disconnect disabled.

The regression connects two real QTcpSockets, selects the first, asserts Disconnect is enabled immediately without a broadcast/mode toggle, clicks it, and proves only that peer disconnects. It then selects the remaining peer, observes it closing remotely, and verifies the placeholder/disabled state.

## P2: changing sequence-enabled UDP to TCP did not persist

The editor changed the configuration's transport after reading the old UDP checkbox and attempted to save `sequenceAnalysis=true` for TCP. The shared schema correctly rejected that configuration, leaving the saved file as UDP while the UI showed TCP.

The edited configuration now clears `sequenceAnalysis` whenever the chosen transport is not UDP, before updating the in-memory profile and persisting it. Offset, endian/window settings, precise budgets and timeout remain intact. The regression edits a sequence-enabled UDP profile through the real profile dialog, checks the saved transport/flag and all preserved settings immediately, closes the window, and reopens it to verify the persisted TCP profile with sequence controls disabled.

## Verification

Final source-stable checkpoint was sent through Orca after the saved-selection preservation correction. The reviewer can rebuild and rerun the unchanged focused cases against this revision; owner changes did not edit the focused test files.

| Check | Actual result | Evidence |
| --- | --- | --- |
| Release `test_ui` / `PortBridge` build | succeeded; no compiler warnings | [build log](../../build/ui-owner/post-review-build.txt) |
| Complete Qt Widgets offscreen suite | 19 passed, 0 failed, 0 skipped; 6.637 s | [QtTest log](../../build/ui-owner/post-review-offscreen.txt) |
| Complete native Windows suite | 19 passed, 0 failed, 0 skipped; 13.690 s | [QtTest log](../../build/ui-owner/post-review-windows.txt) |
| Registered CTest UI target | passed; 7.59 s | `ctest --test-dir build/ui-owner -R '^ui$' --output-on-failure` |

The 19 outcomes comprise 17 behavioral cases plus initialization/cleanup. The four added cases are `exactImportedProfileValuesSurviveCloseAndExplicitEdits`, `statisticsResetPreservesDecoderButRealRecordGapResetsIt`, `selectingLiveServerClientRefreshesDisconnectImmediately` and `sequenceUdpProfileTransportEditPersistsAndRestarts`. Existing send, command, capture, export cancellation/teardown, model bounds, filtering, theme/preferences and UI startup cases also pass. The offscreen plugin emits its expected modal-dialog `propagateSizeHints` warnings; there are no such warnings in the native Windows result.

Independent focused rerun: the final source-stable checkpoint is published; the coordinator/reviewer owns the unchanged focused rerun and acceptance. No independent rerun result had been delivered to this worker when this report was finalized.
