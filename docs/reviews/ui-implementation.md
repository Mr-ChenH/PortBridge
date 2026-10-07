# TASK-006 / TASK-007 — native UI implementation review

The implementation uses Qt 6.8.3 Widgets and the fixed SessionController contract. It contains no embedded HTML, synthetic metrics, automatically established connection, automatically sent command, or fabricated hardware/storage facts. The visual references were the project-factory requirements, interface map, UI and technical designs, prototype HTML/CSS, and preview-dark.png.

## Owned files

- src/ui/main_window.hpp and src/ui/main_window.cpp: native workbench, library/archive pages, operation wiring, preferences and background capture export.
- src/ui/record_model.hpp and src/ui/record_model.cpp: bounded raw sample model and source/HEX/text proxy filtering.
- src/ui/background_job.hpp: shared completion state and bounded join/detach helper with no UI ownership.
- tests/test_ui.cpp: real QWidget/QTest behavior and screenshot artifacts.
- docs/reviews/ui-implementation.md: this review.

No session/network/shared-header/CMake/main source was edited by the UI owner. The coordinator supplied the optional cancellable exportCapture progress callback after review exposed the blocking offline export risk.

## Implemented behavior

The resizable QMainWindow has saved profiles and connection parameters on the left, a real metric strip and finite self-painted trend, a QTableView linked to a byte inspector and recording configuration, and a compact send composer. QSplitter state, window geometry/size, selected profile and dark/light preference persist in QSettings. Microsoft YaHei UI and Consolas are used where available, with explicit Chinese font fallbacks for text alongside numbers. Native focus and named accessible controls remain available; Ctrl+K focuses search, Ctrl+Enter sends/stops a periodic task, Escape stops periodic sending or cancels a connection, and Ctrl+Shift+R toggles recording.

All four transports expose real settings. QSerialPortInfo supplies actual serial names and descriptive tooltips, editable ports remain possible when the list is empty, and QNetworkInterface supplies actual IPv4 addresses. Unsaved default network profiles use loopback addresses. Connection, cancel, disconnect, listening and UDP-bound states are distinct; UDP never promises a live peer. Configuration is frozen while connected/connecting. Connection profiles expose exact byte budgets over the full 1–1,073,741,824 B schema range and timeout values over 1–600,000 ms, preserving valid imported values and dormant fields through untouched closes and saved-profile selection restoration. TCP server targets use actual stable client IDs, with explicit broadcast and selective disconnect; selection immediately refreshes target-specific actions and removal clears stale selection state.

The controller is consumed at a 50 ms GUI timer, without per-packet signal handling; state/client/periodic/recording signals only mark a pending state refresh, while explicit user actions update their controls immediately. Actual snapshots show RX/TX bytes/rates, UDP datagrams or server clients, queue sizes, system buffer results, display omissions and capture outcomes. Zero returned system buffer sizes are described as unavailable, interface speed and NIC/kernel loss remain unknown, and disk speed stays unmeasured/unknown. Diagnostic zero counts never imply recording completeness or absence of network loss. Optional sequence analysis is restricted to UDP datagrams with an explicitly configured uint64 offset/endian/window; it is disabled for unframed TCP and serial streams.

The model retains raw shared payloads and bounded text previews. Its total budget charges vector capacity, peer string capacity and row/preview overhead, with both a 10 MiB budget and a 50,000-row ceiling; an oversized record is omitted whole. Cached row previews are at most 512 decoded characters/64 HEX bytes. Decoder state is bounded to 128 connection/direction/peer identities. Normal TCP/serial mode feeds complete retained blocks into per-connection QStringDecoder state so split UTF-8 characters survive. A separate bounded normal text stream shows complete decoded retained blocks, with source markers when connection changes; it has a 1 Mi-character/10,000-block display limit and visible clipping notices. Display gaps are detected at contiguous batch boundaries using the controller's lifetime record ordinal, independent of resettable statistics. Actual gaps reset decoder state, while statistics reset preserves retained rows/text and partial UTF-8 characters. Explicit display clear/mode/profile changes reset the continuity baseline. High-speed text is explicitly a sampled preview and promises no complete stream reconstruction. UDP text retains datagram separation.

Pausing display preserves reception/counters/recording and freezes existing rows; clear empties display only, and reset statistics is a separate operation. Source/ID, strict HEX bytes and decoded-text filtering act on retained actual samples. Selection exposes real software time/source/direction, full length, offsets, HEX and printable ASCII for up to 4096 bytes, with shown/actual lengths. Copy operates on complete retained bytes; JSON sample export contains actual payloads and explicitly scopes itself to retained, filtered samples.

The send composer calls the shared strict encoder for HEX, UTF-8/ASCII and none/CR/LF/CRLF, exposes an accurate byte count and bounded byte preview, rejects invalid HEX and non-ASCII input, and requires an appropriate live server target. Empty UDP datagrams remain valid. Finite periodic sending freezes all payload/interval/count/target fields while keeping Stop available; disconnect/profile changes stop the old task. Accepted requests are distinct from locally completed TX and peer acknowledgment. History is limited to 50 entries and 2 MiB; input above 1 Mi characters is rejected. Quick commands load without sending.

Commands support add/edit/delete/load, saved persistence, and the shared versioned import/export APIs. Profiles support add/edit/remove/save/import/export using the shared versioned APIs. Failed imports preserve current lists, and profile changes stop the previous session/periodic/recording operations. Restart restores preferences/configuration while remaining disconnected. Changing a profile away from UDP clears its UDP-only sequence-analysis enable flag before saving while retaining its valid offset/endian/window and other configuration values.

Recording uses the actual selected directory, MiB rotation, duration and bounded queue configuration. Running settings are locked. Capacity estimates derive only from current RX+TX rate; QStorageInfo queries the actual existing volume/ancestor path and never supplies invented throughput. Capture listing uses the bounded recent real catalog, including previously persisted files; older files remain available through file selection. Files are never deleted by UI catalog clipping.

Offline capture export runs in one owned background worker. It receives only immutable paths and shared atomic job state, never QWidgets. The GUI polls progress at 50 ms, disables duplicate export starts, offers cancellation, and stays usable while conversion runs. Cancellation returns through the shared callback before QSaveFile commit, preserving the existing output. Window destruction requests cancellation and waits at most three seconds for export completion. Completed jobs join; if filesystem I/O remains stalled at that deadline, the thread safely detaches while retaining only its own shared job state and copied paths, with no UI/controller references or thread termination. Cooperative cancellation continues to be checked before the atomic output commit when I/O resumes. Active files/incomplete files are validated by the shared exporter and reported accurately.

## Verification and screenshot method

Use the matching MinGW 13.1 SDK and Qt 6.8.3 runtime. The owner build directory is build/ui-owner; the standalone SerialPort prefix additionally requires explicit Qt6SerialPort_DIR in this environment. Root targets and test registration remain coordinator-owned.

```powershell
$env:PATH = "C:\Qt\6.8.3\mingw_64\bin;C:\Qt\Tools\mingw1310_64\bin;C:\Users\threeTeeth\workSpace\github\PortBridge\.deps\qtserialport-install\bin;" + $env:PATH
cmake --build build/ui-owner --target test_ui PortBridge
$env:QT_QPA_PLATFORM = "offscreen"
$env:PORTBRIDGE_UI_SCREENSHOT_DIR = "$PWD/build/ui-owner/screenshots"
./build/ui-owner/test_ui.exe -o build/ui-owner/ui-test-results.txt,txt
```

The test executable isolates QSettings/configuration in a QTemporaryDir and registers installed Windows Segoe UI/Consolas/YaHei fonts for the offscreen plugin. If needed QT_QPA_FONTDIR can explicitly point at C:/Windows/Fonts. Screenshots are enabled by PORTBRIDGE_UI_SCREENSHOT_DIR; no data is injected into the shipped application for screenshot generation. Startup images show disconnected zero/unknown state. Server screenshots use actual loopback traffic from two QTcpSockets, including split UTF-8 bytes. Additional minimum/1280-width images support layout review. Using QT_QPA_PLATFORM=windows captures the actual native platform rendering instead of offscreen FreeType rendering. Setting QT_SCALE_FACTOR=1.25 or 1.5 supplies a reproducible additional DPI check.

The suite exercises control names/state and keyboard focus, invalid HEX and actual EOL byte transmission, mode/pause/clear/reset distinctions, finite periodic configuration freeze and stopping, command editing/persistence, real TCP targeting/broadcast/disconnect and per-client UTF-8 decoding, actual recording with Chinese directory/output names, row and retained-capacity limits, filters, profile CRUD/versioned file workflows with rejected unknown versions, actual original-byte sample export, cancellation/output preservation and completed background exports, and preference restoration without automatic connections.

## Post-review corrections and current verification

The independent review's four reproducible UI defects have been corrected, including exact imported profile persistence, UTF-8 continuity across statistics reset, immediate live-client Disconnect state, and sequence-enabled UDP transport edits. The current complete owner suite passes 19/19 outcomes offscreen and on native Windows, and the registered CTest UI target passes. Exact root causes, scope, regression behavior and current logs are recorded in [ui-fixes.md](ui-fixes.md). The evidence below preserves the earlier Task-006/007 implementation checkpoint.

## Original implementation evidence

The suite additionally exercises bounded teardown with a controlled stalled worker, safe retained state after detach, normal completion/join, and cancellation during actual-window destruction. All screenshot and test results below are from real Widgets and actual loopback/file paths, with no claimed physical hardware throughput.

Original Task-006/007 Release owner checkpoint: Qt 6.8.3 / GCC-MinGW 13.1.0, built `portbridge_ui`, `test_ui` and `PortBridge` with zero compiler warnings in owned sources/tests. The Qt offscreen plugin emits its expected `propagateSizeHints` warnings for modal dialogs; the native Windows run emits no such warnings.

| Run | Result | Evidence |
| --- | --- | --- |
| Offscreen, 100% | 15 passed, 0 failed, 0 skipped; 5.698 s | [QtTest log](../../build/ui-owner/ui-test-results.txt) |
| Native Windows platform | 15 passed, 0 failed, 0 skipped; 14.581 s | [QtTest log](../../build/ui-owner/ui-windows-results.txt) |
| Offscreen, 125% | 15 passed, 0 failed, 0 skipped; 7.978 s | [QtTest log](../../build/ui-owner/ui-125-results.txt) |
| Offscreen, 150% | 15 passed, 0 failed, 0 skipped; 8.353 s | [QtTest log](../../build/ui-owner/ui-150-results.txt) |
| Registered CTest UI target | passed; 5.47 s | `ctest --test-dir build/ui-owner -R '^ui$' --output-on-failure` |

The 15 outcomes include 13 behavioral test cases plus test initialization/cleanup. The export regression records 128 actual loopback UDP datagrams of 60,000 bytes each, cancels background JSON conversion without overwriting a preexisting file, completes a later conversion with 128 RX records, and destroys another exporting window while preserving its existing output. The separate completion regression deliberately holds a worker at a controlled wait, verifies the helper returns at its short test deadline and safely detaches retained state, releases that wait, and verifies normal completion can still join.

Visual review compared the reference prototype to the generated native [startup dark](../../build/ui-owner/screenshots-windows/startup-dark.png), [startup light](../../build/ui-owner/screenshots-windows/startup-light.png), [real server dark](../../build/ui-owner/screenshots-windows/loopback-server-dark.png), and [real server light](../../build/ui-owner/screenshots-windows/loopback-server-light.png) screenshots. The native images preserve the graphite/teal and light workbench, panel divisions, table/inspector linkage, Chinese text and actual zero/unknown/loopback states. No prototype data or marketing screen was substituted. [1280×900](../../build/ui-owner/screenshots-windows/startup-dark-1280x900.png) and [1100×760](../../build/ui-owner/screenshots-windows/startup-dark-1100x760.png) retain connect/send/filter/record/interval/count controls within the window. At the minimum size, connection parameters and byte details scroll within their own panels; the data table also allows its own horizontal scrolling. 125%/150% artifact directories are `build/ui-owner/screenshots-125` and `build/ui-owner/screenshots-150`; layout assertions and all behavioral cases pass at both scales.

The trend stores at most 240 timestamped real samples, trims samples older than 60 seconds, and plots against actual elapsed time rather than assuming a timer always fires on schedule. Display omission/cropping counts remain distinct from application RX, recording failure and sequence diagnostics.

## Limits that are not hardware evidence

Real serial hardware, USB removal, two-machine 2.5G load, sustained disk write throughput, and clean-target deployment cannot be certified by these local loopback/native UI tests. No such claim is made by the app or this report. Ordinary-mode text only covers retained display data; omitted data is explicitly marked and raw capture completeness is checked separately. The exporter can cancel at its cooperative I/O checkpoints; operating-system filesystem stalls cannot provide a strict hard real-time cancellation deadline.
