# Independent native UI and integration review

Review date: 2026-10-07. Outcome: **all four reproduced UI defects are fixed and independently reverified; no remaining concrete material UI/integration blocker was found**. The final native product suite passes 19/19 and the unchanged independent focused checks pass 8/8 on both native Windows and font-configured offscreen Qt. The companion network review also closes the independently confirmed retained-pool memory defect.

## Scope and evidence

Read the actual `src/ui/**`, `src/main.cpp`, `tests/test_ui.cpp`, shared `session_controller.hpp` / `types.hpp` contracts, requirements/interface/technical/UI design documents, the high-throughput plan, and the UI implementation report. Inspected native dark/light and 125%/150% screenshot artifacts, and independently built and exercised the real UI using Qt 6.8.3/MinGW GCC 13.1.0. Write ownership is this report, the companion `independent-network-review.md`, and temporary artifacts under `build/ui-independent`; this reviewer made no product/shared/CMake/owner-test edits or commits and created no agents.

Initial source SHA-256 values:

| File | SHA-256 |
| --- | --- |
| `src/ui/main_window.cpp` | `591cf085382826fb22d75a21292afde1a11e8319134a5e3703225a7010c0c9ca` |
| `src/ui/record_model.cpp` | `b8be00fd9a4cc9328743499a46f47743cb3492c05075ef79ab37e52d9f9c120f` |
| `tests/test_ui.cpp` | `dfdeeec1f30f125200651e4d45fff49afb85a40491773742935fe6712eb3f877` |
| `src/main.cpp` | `960c184dd0bab3125e55250f3a97bdf8916bea17cb1b1e79f181fd9bad8bc86b` |
| `include/portbridge/session_controller.hpp` | `39acd3d9cce545846140a6d2b509b6b47b942c47d7380b1a13d424e62df68b28` |
| `include/portbridge/types.hpp` | `714265b99e99b444e102543d366beac532279a67636af8229b90cfac52435d0d` |

`build/ui-independent/hashes-before.json` and `hashes-after.json` each hash 29 source/document/screenshot files. Product source hashes were unchanged during the initial independent tests. Six owner loopback screenshots were regenerated concurrently; their changed hashes are retained in both manifests, so the initial image review is not presented as one immutable final screenshot set. Startup image hashes remained unchanged during that interval.

## Reproducible findings on the original reviewed source — all resolved

The descriptions below preserve the original trigger and consequence. Final dispositions and final-source evidence appear at the end of this report.

All four findings concern actual behavior, not visual preferences. The focused executable links the real product UI/controller and uses isolated profile/settings/capture directories. Initial evidence: `build/ui-independent/focused-before.txt` and `focused-confirmation.txt`. The former has four failures; the latter confirms the four and contains an additional recording setup timing failure that was corrected by waiting for Record enablement. The corrected recording check passes separately and is not a product finding.

### UI-1 — P1: opening and closing silently changes valid profile values

Save a valid UDP profile with receive buffer 12,345 bytes, send buffer 23,456 bytes, send queue 512 MiB, and connect timeout 50 ms. Open MainWindow without editing or connecting, then close it and reload the saved profile. Before the fix, the values become 1,048,576 bytes, 1,048,576 bytes, 256 MiB, and 100 ms respectively.

The form converted byte counts into integral MiB spinboxes and constrained values more tightly than configuration validation. `applyConfig` clamps/rounds them, and `readConfig` in the destructor writes those altered values back. This silently changes transport/queue behavior after an ordinary app visit. Reproducer: `FocusedUi::validProfileFieldsSurviveUneditedClose`; its evidence logs all four before/after values.

### UI-2 — P1: resetting counters can corrupt an incomplete TCP UTF-8 character

Start a TCP server and connect a real client. Pause display, receive `omitted`, and resume so the cumulative display-omission count is nonzero. Receive bytes `E4 B8`, allow the UI pump to retain their incomplete UTF-8 decoder state, reset statistics, then receive `AD`. The byte stream encodes `中`, but the old UI shows a replacement character and no `中`.

The snapshot handler treats any change in `displayOmitted`, including a counter decrease caused by resetting statistics, as new data omission and resets the decoder. Reproducer: `FocusedUi::statisticsResetPreservesPartialUtf8AfterEarlierOmission`. Resetting statistics must not discard continuity of already retained display bytes.

### UI-3 — P2: selected server-client Disconnect enablement is stale

Start a TCP server and connect two real clients. Select one live client from `serverClientTarget`: the old Disconnect action remains disabled because selection change only validates send. Toggle broadcast on/off to provoke an unrelated state refresh, then close the selected client. The target falls back to ID 0, but Disconnect remains enabled because enablement used the selection value captured before rebuilding the list.

Reproducer: `FocusedUi::selectedDisconnectedClientDisablesAction`; the confirmation logs both action states and client IDs. This prevents a valid selective disconnect or presents an enabled action with no target.

### UI-4 — P2: editing a sequence-enabled UDP profile to TCP server does not persist

Save a UDP profile with explicit sequence analysis enabled. Open Edit Profile, change transport to TCP server, and Save. The old form visibly shows server and clears its sequence checkbox, but the stored profile remains UDP with sequence enabled. The runtime banner reports `Sequence analysis requires UDP datagrams; stream framing is not configured`.

Profile editing persisted a copied config's stale `sequenceAnalysis:true` before the transport-dependent form state cleared it. Reproducer: `FocusedUi::udpSequenceProfileCanBeChangedAndPersistedAsServer`; it compares the visible transport against the reloaded profile. Configuration validation correctly rejects this invalid combination, but the UI must prepare a valid config for the user's edit.

## Independent execution and positive checks

Toolchain and root configuration used:

```sh
cmake -S build/ui-independent -B build/ui-independent/out -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_COMPILER=C:/Qt/Tools/mingw1310_64/bin/g++.exe \
  -DCMAKE_MAKE_PROGRAM=C:/Qt/Tools/Ninja/ninja.exe \
  '-DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/mingw_64;C:/Users/threeTeeth/workSpace/github/PortBridge/.deps/qtserialport-install' \
  -DQt6SerialPort_DIR=C:/Users/threeTeeth/workSpace/github/PortBridge/.deps/qtserialport-install/lib/cmake/Qt6SerialPort
cmake --build build/ui-independent/out --target test_ui focused_ui -j 4
python build/ui-independent/run_qt.py windows build/ui-independent/out/product/test_ui.exe build/ui-independent/existing-ui-native.txt
```

The actual Qt log confirms Qt Test/Qt **6.8.3**, shared Release build, GCC **13.1.0**, Windows 11. The runner sets PATH to the project-built SerialPort DLL directory, Qt bin, and matching MinGW bin, sets `QT_QPA_FONTDIR=C:/Windows/Fonts`, and writes reviewer screenshots into `build/ui-independent/screenshots-windows`. The focused test also registers Segoe UI, Consolas, and Microsoft YaHei files explicitly for offscreen runs. The explicit project `Qt6SerialPort_DIR` is necessary for this installation.

Original product suite: **15 passed, 0 failed, 0 skipped, 12,364 ms**, including init/cleanup. Evidence: `build/ui-independent/existing-ui-native.txt`. It covers startup/accessibility, payload validation, actual RX/TX, pause/clear/reset, finite periodic sends, commands/profiles, server targeting/stateful text, real capture and export cancellation, bounded background teardown, display model bounds, sample export, and preference restoration without automatic connection.

Additional independent positive checks: **4 passed, 0 failed, 409 ms**, including init/cleanup, in `positive-focused.txt`:

- Ctrl+C with selected text in the send editor still copies that editor selection.
- A real UDP capture continues through Pause and Clear Display; RX counters remain real, recording stays active, completion is published, and exported RX bytes reconstruct exactly `pausedresumed`.

These passing checks do not cancel the four reproducible failures above.

## Screenshot review and acceptance limits

Actually opened native dark startup at 1100×760, native light loopback, 125% dark startup, and 150% dark startup / light startup / server loopback images, together with independent native screenshots. Checked text rendering, primary action visibility, connection/send/statistics separation, scrolling/resizing behavior, and dense server data presentation. No additional concrete material layout defect was established from the inspected images; no finding is based on aesthetic preference alone.

Hardware serial loopback/unplug behavior, physical two-machine 2.5G throughput, sustained disk recording, multi-GB export performance, and clean-system deployment remain unverified. Screenshot and localhost evidence establish only the conditions actually exercised. The companion `docs/reviews/independent-network-review.md` records eight independently passing final network groups, short genuine benchmarks, controlled checksum/sequence injections, stable final network/validation hashes, and the resolved retired-pool ownership finding.

## Final fix verification

The coordinator supplied a source-stable UI checkpoint after the owner fixed the four findings and blocked profile-selection signals during saved-index initialization. Independently rebuilt the real `test_ui` and unchanged `focused_ui` targets against that final revision and the corrected final network library. All checks below pass; totals include init/cleanup.

| Final execution | Actual outcome | Evidence |
| --- | --- | --- |
| Product UI suite, native Windows | **19 passed, 0 failed, 0 skipped; 13,117 ms** | `build/ui-independent/existing-ui-final-native.txt` |
| Unchanged independent focused checks, native Windows | **8 passed, 0 failed, 0 skipped; 3,198 ms** | `build/ui-independent/focused-final-native.txt` |
| Unchanged independent focused checks, offscreen with installed fonts | **8 passed, 0 failed, 0 skipped; 1,278 ms** | `build/ui-independent/focused-final-offscreen.txt` |

Commands for the final independent runs:

```sh
cmake --build build/ui-independent/out --target test_ui focused_ui -j 4
python build/ui-independent/run_qt.py windows build/ui-independent/out/product/test_ui.exe build/ui-independent/existing-ui-final-native.txt
python build/ui-independent/run_qt.py windows build/ui-independent/out/focused_ui.exe build/ui-independent/focused-final-native.txt
python build/ui-independent/run_qt.py offscreen build/ui-independent/out/focused_ui.exe build/ui-independent/focused-final-offscreen.txt
python build/ui-independent/final_manifest.py
```

| Finding | Final disposition and independent observation |
| --- | --- |
| UI-1 / P1 | **Resolved.** Exact byte-value controls and full valid ranges preserve RX 12,345 B, TX 23,456 B, queue 536,870,912 B, and timeout 50 ms through an unedited close. Final product regression also covers restored-index initialization and explicit edits. |
| UI-2 / P1 | **Resolved.** Decoder continuity follows the lifetime record ordinal, independent of resettable omission counters. `E4 B8`, statistics reset, then `AD` displays `中` with no replacement character. The product regression also tests a real record gap causing a decoder reset. |
| UI-3 / P2 | **Resolved.** Selecting a live target enables Disconnect immediately; closing it falls back to target ID 0 and disables Disconnect while the other client remains connected. |
| UI-4 / P2 | **Resolved.** Editing sequence-enabled UDP to TCP server saves/reloads server with sequence analysis false; no validation banner remains. The product regression includes restart with the saved transport. |

The two positive focused checks remain passing after the fixes: Ctrl+C copies editor selection, and Pause/Clear retain the running capture and exact exported RX bytes. Final native screenshots were regenerated by this reviewer's final product suite; the inspected startup image is unchanged, and the current loopback server image was opened again. No new material visual defect was established.

Final source/image SHA-256 values:

| File | SHA-256 |
| --- | --- |
| `src/ui/main_window.cpp` | `729b650b1ba9784af0e48ce416dde447e1d7a8711f538cb800edc22e9d5f30c4` |
| `tests/test_ui.cpp` | `7174749c7d1af2ec0997d57f68cf9eccd508df9d8e1d0b2628325287657161ba` |
| `src/ui/record_model.cpp` | `b8be00fd9a4cc9328743499a46f47743cb3492c05075ef79ab37e52d9f9c120f` |
| `build/ui-independent/screenshots-windows/startup-dark-1100x760.png` | `ac75ccc05cbe054c6b97c0889fd90e814b5660798697f9b7f4c4dedc3bf41576` |
| `build/ui-independent/screenshots-windows/loopback-server-dark.png` | `6a3a27fdcf8850fc8a85d159ae013ede9fc0df1febae0f116ac99c5fd19aa8c4` |

`hashes-latest-pretests.json` and `hashes-latest-posttests.json` preserve the final verification interval. `build/ui-independent/hashes-final.json` additionally hashes final source/contracts/design inputs, owner and reviewer screenshots, test evidence, and tested executables. No product input changed between the latest pretest snapshot and final verification. Shared controller/types and record-model hashes remain unchanged from the initial review. The two reports are the durable review outputs; temporary focused sources and logs remain available for reproduction.

