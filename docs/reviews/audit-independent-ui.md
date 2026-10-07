# Independent final UI audit repair review

Date: 2026-10-07. **Final independent UI verification passed:** native Windows at100%,125%,150% each passed8/8 independent and31/31 product Qt results; the full reviewed source manifests are identical before/after and across all three runs. No remaining concrete UI blocker was identified within this review's scope. This report covers independently reviewed UI implementation; this reviewer made no UI production or UI owner-test changes.

## Scope and source review

Read the original function/design audit, `docs/project-factory/04-ui-design.md`, final `src/ui/main_window.cpp`, `main_window.hpp`, `record_model.cpp`, `record_model.hpp`, `design_widgets.hpp`, `background_job.hpp`, shared/session consumers, and owner tests. The separate session/benchmark review and its explicitly reviewer-authored narrow repairs are documented in `audit-independent-session.md`; their independence limits do not apply to this UI review.

Profile activation stops and selects controller configuration before clearing samples, stream, decoder state, sequence display, trend, banner/status messages and inspector. Constructor, selection, add/edit/delete/import share activation. Valid unsaved old configuration is read before changing the profile identity. Current selection/filtered model changes clear unavailable inspector payload and disable copy actions. HEX/ASCII are bounded to the same first 32 bytes while the copy operation uses the original payload.

Receive truncation has its own UI stage/diagnostic count and remains separate from application queue loss. Sequence ratio uses finalized `sequenceMissing/sequenceExpected`, excludes duplicate/pending positions, and explicitly shows unknown when the denominator is zero. TCP/serial byte streams do not receive synthetic datagram loss claims. Preferences restore display/recording/send parameters but reopen with no connection, recording or periodic sending.

Sample export captures bounded immutable payload handles and metadata by value. Capture export/deletion and storage queries capture shared job state plus paths by value; their worker bodies do not capture QWidget/model/controller references. Completion publishes result data with acquire/release synchronization. Destruction requests cancellation and uses bounded completion waiting; detached stalled workers retain their own state. Storage completion rejects a stale path and schedules the latest pending request. Atomic QSaveFile cancellation preserves existing output. These are source findings plus finite local probes, not proof against every permanently stalled filesystem call.

## Independently chosen probes

`build/audit-independent-ui/independent_ui.cpp` links actual product libraries via the isolated CMake project and has six substantive cases plus initialization/cleanup:

| Case | Evidence |
| --- | --- |
| Full command interval range | Persist valid periodic commands with interval 1 and 86400000/count 0; load without sending, edit only the name, save and verify the exact interval/count survived. |
| Traffic, inspector, ratio and isolation | Actual loopback UDP sequence 100 then 102/window 1 produces 50% with denominator 2; duplicate 1472-byte payload yields matching 32-byte HEX/ASCII scope. A typed simulated truncation increments its separate stage, admits no RX, and stays connected. Selecting TCP clears counters, errors, samples, inspector and copy actions. |
| Profile creation and filtered inspector | A valid unsaved port 34567 survives adding another profile and returning; actual persisted profile matches. Filter hides selected original bytes and disables copying. |
| Immutable sample export and close | Export a 3 MiB retained payload; select another profile while its worker runs; JSON retains the original payload although the model clears. Closing during a second export preserves an existing KEEP output. |
| Catalog and capture worker | Write a real 256-record/65507-byte-per-record capture through the real recorder admission seam, wait for durable completion; refresh copied/invalid-sidecar/deleted files, preserve selection, show unknown duration/transport, then cancel capture export and close, preserving existing output. No physical network origin is claimed for this recorder input. |
| Continuous send and inert restore | Real UDP sink receives count=0 periodic traffic for at least five sends; stop leaves TX unchanged. Reopen preserves display format/autoscroll/queue/interval/count but starts no connection/recording/periodic task. |

The product UI suite supplements these probes with CRUD/import/delete, bounded display/cache behavior, row actions, send targets, raw stream decoding, recording/export, bounded detached completion and native layout assertions. Root/product session/catalog/sequence stress coverage is in the separate session report, not duplicated here.

## Findings and resolutions

An early source read found composer/dialog intervals constrained to 10..3600000 while the shared command contract permits 1..86400000. A valid imported command would silently change when loaded or saved after a name-only edit. The coordinator had independently identified the same issue, and the UI owner repaired it before the preliminary probe was built. The independently chosen endpoint/name-edit probe and owner permanent full-range import regression pass on the repaired implementation. No UI implementation patch was authored by this reviewer.

The owner also repaired unknown duration presentation when metadata is invalid, delegate double drawing beneath custom row widgets, primary connect/bind contrast, full ASCII preview visibility, Chinese dialog labels/EOL, custom periodic row/chip metadata, and continuous count semantics. Source review, native images and relevant probes confirm those final behaviors.

Earlier validation evidence is intentionally retained: a first final run detected a production source edit after readiness, so its source-stability gate was rejected and the libraries rebuilt. A later native1x guard detected an owner-test-only change while tests themselves passed, so the final test target was rebuilt. A prior compiled-test 150% run passed all real TX/receive/byte checks but failed the transient nonzero TX-rate-label assertion; that failure is preserved in `product-acceptance-scale-1.5.txt` and was reported to the coordinator. The UI owner subsequently made the TX fixture latch admission and observe rate during actual continuous traffic, which avoids depending on a transient single-datagram sample. The final rebuilt owner-test source passes at all three scales; the compiled object postdates that final source. Final rebuilt-source results below distinguish those attempts from accepted evidence.

## Native visual review

Actually opened all eleven owner `build/audit-ui/screenshots/audit-native-*.png` images: UDP, TCP client, recording, diagnostics, capture settings, captures, commands, command/profile dialogs, and 1100x760 workspace/commands. Also opened owner125% commands and150% minimum-size workspace, independently captured UDP/unknown-metadata catalog, and regenerated product150% UDP. Observed one clear row text layer, visible row actions, green primary actions, the 32-byte range/ASCII, explicit UDP endpoint/unknown metrics, localized dialog actions, and accessible controls in the 1100x760 layout. Native captures were produced using Qt Windows, not the offscreen renderer. Final screenshots are regenerated by the independent driver under the evidence directories below.

The 150% monitor emitted native geometry and a Fixedsys DirectWrite warning in earlier runs. The recorded layout assertions passed, and PNGs preserve the actual rendered geometry. These checks do not certify every physical monitor/work-area combination or every fallback font installation. No new visual redesign/polish scope was added.

## Reproduction and final evidence

Toolchain: Qt6.8.3/MinGW13.1/Ninja with project-local Qt SerialPort. Configure `build/audit-independent-ui` as source and `build/audit-independent-ui/out` as build using the same compiler/prefix/SerialPort flags documented in the session review, then build `independent_ui test_ui`.

```sh
cmake --build build/audit-independent-ui/out --target independent_ui test_ui -j 4
python build/audit-independent-ui/verify.py --phase certified --scale 1 --product
python build/audit-independent-ui/verify.py --phase certified --scale 1.25 --product
python build/audit-independent-ui/verify.py --phase certified --scale 1.5 --product
```

The driver sets matching runtime PATH, unsets QT_QPA_PLATFORM for native Windows, sets the requested scale, records exact commands/returns/logs and source SHA-256 manifests before/after each run. `acceptance.py` runs those final gates sequentially in a background process so a60-second shell-tool limit cannot terminate the suite.

Final accepted evidence:

| Native scale | Independent results | Product results | Identical source before/after |
| --- | --- | --- | --- |
|100%|8/8,6045 ms|31/31,29093 ms|Yes|
|125%|8/8,7287 ms|31/31,29140 ms|Yes|
|150%|8/8,6485 ms|31/31,26078 ms|Yes|

All commands returned0. Full source manifests are identical across the three runs and still match current production/owner-test files. Authoritative evidence is under `build/audit-independent-ui/`: `results-certified-scale-{1,1.25,1.5}.json`, `probes-certified-scale-*.txt`, `product-certified-scale-*.txt`, and `hashes-before/after-certified-scale-*.json`. `acceptance-complete.json` records all three passes. Own probe snapshots are in `screenshots-certified-scale-*`; the actual owner product screenshot method was regenerated natively in `product-screenshots-certified-scale-*`. Final regenerated1x UDP,150%1100x760 commands and125%clean-new-profile images were also opened and reviewed.

Key final SHA-256 hashes:

|File|SHA-256|
|---|---|
|`src/ui/main_window.cpp`|`d4e53471002ee93f0657526936c8393417ba8f8764d4e34b75de84adb77b0f30`|
|`src/ui/design_widgets.hpp`|`c4e1bbcd005347e3b91bbdf8767d703282d67f62f7739e2f2d7be414c278995d`|
|`src/ui/record_model.cpp`|`b8be00fd9a4cc9328743499a46f47743cb3492c05075ef79ab37e52d9f9c120f`|
|`src/ui/background_job.hpp`|`a850a7d4516cda53ac3d566e1b14d31f716eb9fbe6d3f9bdbe5dd8b9a8bffe89`|
|`tests/test_ui.cpp`|`20e75eb9ec04a2f1ae109ad651f2a6787ad2656b6bbb6ee18f1863656aaf4eb7`|

## Limits

Actual UDP traffic and local recorder/export/catalog filesystem work were exercised. Typed truncation is simulated at the existing session callback seam; physical truncation and physical serial are not claimed. Export/storage worker source lifetime was reviewed and local cancellation/destruction exercised; no indefinitely stalled network share or resource-exhaustion injection was performed. No sustained2.5G throughput, RSS envelope, packaged/clean-machine release, exhaustive Qt event ordering, exhaustive scanner schedule or all-monitor visual acceptance is certified. Release/archive/deployment acceptance belongs to the coordinator.
