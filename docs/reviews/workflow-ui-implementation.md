# Native workflow UI implementation

Worker B owns `src/ui/workflow_*` and `tests/test_workflow_ui.cpp`. This report covers the native editor and its bindings to the real backend, rather than the browser prototype. Root CMake, application navigation/resource replacement, backend ABI, protocol implementation, and packaging remain coordinator/A/C responsibilities.

## Implementation

- `workflow_page.hpp/.cpp`: `portbridge::WorkflowPage` with the required runner/provider/preparation API and side-effect-free document/file/template APIs. Native heading/title, dirty status, searchable grouped palette, split canvas/inspector/log layout, template dialog, parameter/result tabs, protocol-specific forms, log and typed variable tables, run/pause/stop, and explicit overview.
- `workflow_graph_model.hpp/.cpp`: `AbstractGraphModel` backed by `WorkflowDocument`, stable business UUIDs and retained graph IDs across undo, stable edge IDs across removal/restoration, authoritative backend port definitions, single-edge outlets, no self-connections/start inputs, ordinary cycle prevention, and model-level mutation locks. No data-flow model or execution callbacks are used for graph edits.
- `workflow_canvas.hpp`: native QtNodes geometry/painters, serpentine port direction derived from saved graph layout, actual Qt drag MIME creation/drop handling, current/completed/error node and edge painting, native mouse port wiring, readable 85% regular view and explicit overview, keyboard palette activation and Ctrl+L port connection dialog.
- `tests/test_workflow_ui.cpp`: native and offscreen interaction/regression coverage. Real protocol cases compile with `PORTBRIDGE_WORKFLOW_REAL_PROTOCOL`; fixtures serve actual localhost UDP, HTTP, and RFC6455 WebSocket traffic.

Forms expose canonical HTTP method/URL/headers/body/timeouts/response limits/expected status/output; WS URL/headers/subprotocol/CA/TLS/message limits; message resource and text/binary type/HEX/variable payload; raw profile configuration snapshots and borrowed/owned policy; explicit target/client/broadcast, source filter, native framing mode/length fields plus advanced framing JSON; extraction, comparison relations, finite loop count, variables and logs. Profile selection stores a config snapshot, never a permanent profile-list index. TLS controls follow the actual protocol policy: server certificate chain/hostname verification stays enabled.

The page starts with localhost examples and no results. Loading, selecting templates, editing parameters, dragging nodes/ports, saving and importing do not call the runner. Start validates the graph, calls the caller's preparation hook, and passes a fixed value snapshot to the real runner. Active runs lock model and forms; pause affects future steps, stop remains visible when the log collapses, and failed/current/log-selected nodes are brought into view. Preparatory rejection, success, failure/timeout, pause and cancellation retain truthful text and real results.

## Persistence and bounded UI data

Files use backend `.pbflow.json` schema and atomic `QSaveFile`; import caps input at 16 MiB, rejects `prototypeOnly` with instructions to recreate from a native template, and surfaces unsupported-node/schema errors without replacing the current graph. Template replacement is undoable; import/close has Save/Discard/Cancel. During an active dirty flow, Save is labelled **停止并保存**; cancelling either confirmation or destination preserves activity, and stopping happens only after affirmative Save plus a destination.

Undo has an 80-entry limit and a 16 MiB retained serialized-history estimate, accounting for whole-document/property commands and deleted graph content; over-budget history is cleared while the current graph is retained. Clearing history also prunes the graph-ID/edge-ID history maps. Logs are appended incrementally and capped at 2,000 rows (or the smaller workflow limit), variable display at 1,024 rows, and individual variable cells at 4,096 characters. Result preview trims before conversion/serialization with a shared 32,768-character budget, entry/depth bounds, and an explicit truncation note. Large parameter fields become read-only bounded previews rather than silently truncating saved values. Deliberate result export retains full bounded backend output after redaction.

Redaction covers credential-like keys regardless of value type, nested objects/arrays, JSON string bodies, header/Bearer/query text, statically named credential variables, and known runtime secrets in free text. Secret detection retains at most 1,024 bounded samples: short secrets up to 4,096 characters and 128-character prefixes for longer secrets; fields matching a long prefix are masked entirely. Collection has a shared 65,536-entry traversal budget and depth 24. Encoded payloads are scanned in 65,536-character Base64 chunks with a 16,384-byte overlap; encoded structured data, credential-shaped text, known secret samples and malformed encodings are conservatively hidden. The separately redacted parsed body and bodyText retain readable nonsensitive information. This may conceal benign encoded JSON; ordinary nonstructured WS binary byte results remain inspectable. Result/log/variable/canvas/file/export views use this policy; real runtime credentials are never inserted into document state. Saved literal credentials are replaced with an explicit marker and the page explains that reopening needs variable references or re-entry.

Editor/log splitter sizes and collapse state persist without restoring or starting a run. At compact desktop widths the palette folds; both sidebars and the log can be folded to keep the canvas usable. Fields, actions, tables, dialog controls and canvas have accessible names/descriptions. Native text editing keeps its own undo/copy/delete shortcuts; graph shortcuts are scoped to the canvas.

## Verification evidence

Isolated build: `build/workflow-ui`, Qt 6.8.3 / MinGW GCC 13.1 / C++17, normal `-Wall -Wextra -Wpedantic` flags. Dependency compilation initially used the explicitly unavailable protocol seam in `build/workflow-core-check/protocol_stub.cpp`; those checks proved editing, logic/raw execution and UI behavior only. After actual protocol archives became available, the isolated CMake switched to the real protocol/cpr/curl/c-ares/OpenSSL archives, without writing another worker's build. A stale root product archive was rejected at relink after the WebSocket peer-close ABI changed; the matching final C build at `build/workflow-protocol/native` resolved it. The current archive receipt is SHA-256 `b40f12b238ec137143c63bde04ed5de134d229a2d96454bc43891c5d4ba04af0`; no protocol seam is used for final verification.

Final frozen-source evidence (all real-protocol checks use the matching C archive):

| Evidence | Result / scope |
| --- | --- |
| `build/workflow-ui/native-real-tests.txt` | **26/26**, actual Windows widgets and real logic/UDP/HTTP/WS fixtures; 25,976 ms |
| `build/workflow-ui/offscreen-real-tests.txt` | **26/26**, offscreen with Windows fonts configured; 22,136 ms |
| `build/workflow-ui/native-real-tests-125.txt` | **26/26**, native Qt scale factor 1.25; 16,536 ms |
| `build/workflow-ui/native-real-tests-150.txt` | **26/26**, native Qt scale factor 1.5; 18,380 ms |
| `build/workflow-ui/long-credential-tests.txt` | **11/11**, lifecycle plus nine actual HTTP credential data rows with actual export dialog |
| `build/workflow-ui/build-real.log` | No compiler warnings/errors; current matching-ABI executable links successfully |
| `build/workflow-ui/screenshots/native-editor-{dark,light}.png` | Actual 1024×768 Windows editor/theme screenshots |
| `build/workflow-ui/screenshots/native-editor-{dark,light}-{1280x900,1440x1000}.png` | Complete two-row native graph, continuous row transition, separate reverse captions/details |
| `build/workflow-ui/screenshots-125`, `screenshots-150` | Current-source native scaled screenshots and screen/DPI receipts |

Earlier `native-tests.txt`, `offscreen-tests.txt`, `native-tests-125.txt`, `native-tests-150.txt` remain intermediate unavailable-protocol-seam evidence. They are superseded for final acceptance by the four current real-protocol logs above.Actual mouse node movement and output-to-input wiring, deletion/restoration of connected nodes and stable edge identity, native palette/drop events, field mutation/undo, dialog templates, JSON roundtrip/prototype rejection, validation field focus, real results/log selection, preparation refusal, immutable active graph, pause/stop, sampled-display UDP response matching and timeout focus are covered. No mocked product results are injected.

Native checks found and repaired zero node opacity from an empty QtNodes style, late destructor callbacks into a destroyed implementation, stale node paint across theme changes, inherited heavy node shadows, over-small text, incomplete native mouse-doubleclick fixtures, and a tiny expanded log restored from a prior collapsed height. A's integration review also prompted the active Stop+Save close flow. A transient framing-editor `QJsonObject::value(key, default)` compile error was fixed to a contains/value fallback and all subsequent UI compiles were clean.

The independent review identified two masking regressions after the initial acceptance: a 5,000-character token was omitted from the short-secret list and remained reversible in Base64, then typed/escaped credentials bypassed text-only encoded recognition. Both are repaired with bounded long-secret samples and conservative encoded structured-data masking. The final focused test passes **nine** actual HTTP rows (11 including lifecycle): 5,000-character string, encoded-chunk boundary, numeric, boolean, null, array, object, escaped Unicode numeric key and a 300,000-character padded response carrying `clientCredential`. Each verifies raw backend truth, result preview, displayed logs/variables, ordinary clipboard copying and actual Qt result save-dialog export; raw runner bytes remain unchanged. Large structured text above 262,144 characters is masked without GUI JSON parsing; bounded chunk sampling captures credential string prefixes for masking echoed log/variable text.

The latest native screenshot review also repaired reversed-card detail/error-caption overlap and a discontinuous row-transition curve clipped to QtNodes' own connection bounds. Geometry coverage asserts the connection stroke stays within those bounds, and actual 1440×1000 dark/light images confirm readable reverse labels and continuous snake connections. Focus returns to the canvas after keyboard port-dialog close; revealing a selected node preserves full graph context when already visible.

## Final verification status

Implementation is complete and frozen. The final 26-case suite passes native Windows, offscreen, native Qt 125% and native Qt 150% with zero failures/skips. C independently confirmed the **same final 26-case source**, its own **10/10** genuine HTTP preview/export checks, and native 125%/150% screenshot checks (**3/3** each); P1 masking and P2 label findings are closed. Its final receipt checked 48 source and 13 artifact hashes with zero mismatches, retaining historical failed evidence. The coordinator's first freeze guard overlapped the last required P1 additions; the superseding explicit freeze and matching final-source checks above resolve that receipt discrepancy. No source/test changes were made after the latest explicit freeze acknowledgment.

Final source SHA-256 receipts:

| File | SHA-256 |
| --- | --- |
| `src/ui/workflow_canvas.hpp` | `90a0cca511e612d8b1b7bc9fa67e2e719478f093def7e826c96170d944648591` |
| `src/ui/workflow_graph_model.cpp` | `4e9b15f22649426ded45a9087e03a3301a57fa363400aa4f21f6cfb4f66a2398` |
| `src/ui/workflow_graph_model.hpp` | `8cd2fd3f08f517ce9dda195be438a0feed28687d4979ba2dd6794cb7f3780ae8` |
| `src/ui/workflow_page.cpp` | `620cbfd6b7a40bae9734c7a1e8476b8f75158182b600cfed47658a290fe69d8b` |
| `src/ui/workflow_page.hpp` | `bdc2c62fa2775c3b95154ee8160b432090dde83b215356895ecf78caf64bd082` |
| `tests/test_workflow_ui.cpp` | `8c9a14b01f6f582b3b1e586706cb3fa2b7e06926b919810f4f81046c8896b14f` |
| isolated `test_workflow_ui.exe` | `0e4766a7bd0c687a664fa7c84f055a8e1117407283b92f23e962d20e284c61b7` |

Actual screen receipts: at DPR 1 and 1.25 the widget is 1024×768; at DPR 1.5 the available desktop is 1280×688 logical and Windows constrains the requested 1024×768 widget to 1024×707 including frame. Canvas remains 734×332 and normal zoom 0.85, with accessible visible stop and fold controls. Qt scale-factor checks do not claim physical OS display-scaling coverage.

Final root regressions, real HTTPS/WSS/TLS/cancellation acceptance, full application navigation/resource conflict testing, actual OS display-scaling changes and packaging remain coordinator-owned. Worker B's native editor/runtime binding scope and C's independent P1/P2 review are complete.
