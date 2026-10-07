# PortBridge session implementation review — TASK-003/004/005

Status: implemented and owner-verified on Windows 11 with Qt 6.8.3 / MinGW GCC 13.1.0. This report covers only `src/session/**` and `tests/test_session.cpp`; shared headers, CMake, network, UI and application entry point were coordinator/other-worker owned. No Git commits or nested agents were created.

## Implementation and contracts

`SessionController` belongs to the GUI thread. Network and serial callbacks update a mutex-protected bounded raw-data pipeline and state/error mailbox; a precise 50 ms timer emits GUI-facing state, sample and statistics notifications. No queued GUI signal is posted per received packet. Records keep immutable shared raw byte vectors, original I/O timestamps, and an accepted-record ordinal. TCP retries reuse their original timestamp and do not increment traffic counters or the ordinal until admitted. The ordinal is unrelated to a device/protocol sequence number.

Transport callbacks capture an immutable session generation. Stop/switch invalidates the generation before a new engine can deliver data. TCP IDs map monotonically to public IDs shared by `clients()`, data records and send/disconnect targeting; serial and UDP use separate ID namespaces. Old engine cleanup runs on a bounded background reaper, so canceled Windows DNS cannot block a profile switch. At most four retiring engine generations are admitted; another switch is explicitly rejected until cleanup progresses.

Serial `QSerialPort` and `QTimer` objects are created and operated entirely in a dedicated `QThread` event loop. Device discovery uses `QSerialPortInfo`; a manually entered name still works. Parameter/open/resource/write failures surface through transport events and explicit terminal state. `NoError` is ignored. RX can hold one rejected block while the recorder applies backpressure; configured serial read buffering and bounded event-loop turns prevent an unbounded receive loop. TX reports bytes only from `bytesWritten`, including actual partial completions. Both serial send bytes and outstanding write descriptors are limited.

Normal display retention is at most 50,000 records and 10 MiB; high-speed retention is at most 2,000 records and 2 MiB, admitting at most 20 samples per 50 ms pump interval. Admission charges vector **capacity**, record/deque allowance and peer-string capacity, rather than payload length alone. Taking samples returns at most 256 records per call with a 1 MiB payload batch target. Pause skips new display samples while RX/statistics/recording continue; clear affects only the display queue. Display omissions, application pipeline admission drops, and recording failures have separate counters. Transport-owned pools and the writer's bounded in-flight batch are additional bounded allocations, not hidden inside the display byte limit.

Periodic sending validates its interval/count/payload, stops on rejection/disconnection/switch/user stop, and never retries an already admitted send because recording failed. `periodicSent()` counts admitted periodic requests; `Statistics::txBytes` counts actual local write completions. TCP retries a receive block when the recording queue can eventually admit it; an impossible-size block terminates recording with an explicit incomplete result. UDP recording overrun counts the real RX operation, application pipeline drop and recording failure, then continues. Completed TX is always counted and never resent because its recording admission failed.

Lifecycle/error/STOP events also enter the same bounded pipeline as `Direction::System`. Their UTF-8 JSON payload contains `schemaVersion`, event category, message, connection identity and local endpoint; the record retains peer/time/transport metadata. These records do not change RX/TX bytes or UDP datagram counts, never request transport retry, and never recursively create transport errors. Text decoding must keep SYSTEM separate from stream RX/TX.

## Binary capture v1

The background writer batches records, rotates whole records by a target file size and supports a finite duration or continuous recording (`durationSeconds == 0`). Oversized individual records occupy their own file rather than splitting a datagram/reading block. Queue admission charges retained capacity and metadata, with a separate maximum of 65,536 queued descriptors. The writer drains batches with a 4 MiB charge target; one whole record may exceed that target, with an absolute 64 MiB payload format limit.

All integers in `.pbc` are little endian. Files use a UUID plus recording time and part number. Payload bytes are never converted to HEX/JSON while recording.

| Region | Fields |
| --- | --- |
| 24-byte header | ASCII `PBCAP001` (8 bytes), version u32 = 1, header size u32 = 24, started time u64 in Unix microseconds |
| DATA prefix | ASCII `DATA` (4), body length u32 |
| DATA fixed body, 44 bytes | ordinal u64, timestampUs u64, stable connectionId u64, transport u8, direction u8, reserved zero u16, peer UTF-8 length u32, peer port u16, reserved zero u16, payload length u64 |
| DATA variable body | peer UTF-8 bytes, followed immediately by raw payload bytes |
| 32-byte finalization footer | ASCII `DONE` (4), body length u32 = 24, record count u64, stored payload byte count u64, complete flag u8, seven reserved zero bytes |

Transport values are Serial=0, TcpClient=1, TcpServer=2, Udp=3; direction values are Receive=0, Transmit=1, System=2. `CaptureInfo::bytes` and recorded byte statistics count stored payload bytes, including SYSTEM JSON payloads, rather than the physical `.pbc` file size. The file size rotation target includes format overhead.

A `.pbc.meta.json` sidecar is written with `QSaveFile`; it starts incomplete. It contains schema version, path, start/duration, payload byte/record counts, completeness and error. Unsigned 64-bit values use decimal strings. Published `CaptureInfo.complete` remains false until **both** binary footer/flush and the atomic sidecar commit succeed. Footer, flush and catalog failures halt subsequent rotation; catalog failure also attempts to mark the binary footer incomplete. Recording queue overflow can never silently claim a complete capture.

The in-memory capture catalog retains the latest 1,024 entries, including an active entry. Older user files remain on disk and can be exported explicitly. Startup and directory scans run in background workers, stream `QDirIterator` entries into a bounded recent-file selection, and read only bounded metadata. Missing/corrupt sidecars produce an incomplete/unknown catalog entry rather than a fabricated success. Active entries are located by path, so catalog eviction cannot invalidate an active index.

Stop requests background finalization. Recorder destruction waits at most three seconds; a delayed writer retains its own shared state and marks its eventual result incomplete after a timeout. Initial incomplete metadata and the mandatory footer prevent an interrupted process from claiming a complete capture. Engine cleanup likewise waits at most three seconds during controller destruction, then can continue in worker-owned state with the callback lifetime gate closed. OS DNS or filesystem operations cannot be forcibly made instantaneous; slow-I/O shutdown has a bounded owner wait, and unfinished binary files fail decoder finalization checks.

`exportCapture` validates magic/version/header size, tags, reserved values, transport/direction enums, declared lengths, peer UTF-8, complete footer counts, truncation and trailing bytes. Payloads are limited to 64 MiB and peers to 4,096 bytes before allocation. It streams records to a `QSaveFile` output, emits reconstructable JSON/Base64 or metadata plus HEX text, and preserves prior output on failure or cancellation. A progress callback receives processed/total source bytes, may cancel, and is checked before final commit. Export runs on the calling thread; GUI integration must use its background export worker. Same-file/case-alias/canonical-path alias exports are rejected. Incomplete but finalized captures export with `complete=false`; truncated/unfinalized captures fail without replacing the output.

## Configuration, commands and codec

Connection profiles and commands use JSON `schemaVersion: 1` with strict field sets/types/ranges and a consistent 16 MiB serialized/read budget. All declared connection fields round-trip, including serial settings, addresses/ports, socket/queue budgets, timeout and sequence settings. Import first validates a candidate vector and replaces the destination only on complete success. Export/save uses `QSaveFile` and preserves an existing file on invalid input or oversized serialization. Corrupt profiles return the four default transport profiles plus an error; corrupt commands return an empty list plus an error. Startup never connects or sends.

Default storage is `QStandardPaths::AppDataLocation`, with a `QSettings` `storage/directory` override. `recording/directory` remembers the most recently requested recording directory. Paths and names cross the `std::string`/Qt boundary using explicit UTF-8 conversion, including Chinese filenames and serial/profile/command names.

HEX accepts whitespace and complete pairs of ASCII hexadecimal digits only; invalid characters and odd digit counts report a precise error. Text supports ASCII/UTF-8 and rejects non-ASCII input, unsupported encodings and malformed UTF-16 surrogates. EOL is strictly none/CR/LF/CRLF. Empty UDP payloads remain legal.

## Sequence analysis policy

Analysis defaults off/unknown and is accepted only for explicitly configured UDP datagram fields. Arbitrary serial/TCP read chunks cannot define protocol frames; enabling sequence analysis on a stream profile is rejected with an explanation. The datagram field is an unsigned 64-bit value with configurable byte offset and endian. Too-short datagrams remain valid RX but do not contribute a sequence observation.

The finite reorder window uses modular uint64 serial arithmetic: a forward difference smaller than 2^63 advances the current epoch; wraparound increments the internal epoch. An exact half-range jump is ambiguous, finalizes the known prior interval and starts an explicitly reported new analysis epoch. Confirmed duplicates are retained-window observations; a first late observation counts as reordered. Values older than the finalized window count as late/reordered, but duplicate identity outside retained history is not recoverable. Missing values are finalized when they leave the window or when the session stops/disconnects. Large gaps use arithmetic plus bounded retained entries, without allocating proportional to the gap.

At most 64 peer windows are kept, with a global retained-entry budget around 1,048,576 plus bounded initial observations. Before a peer/memory eviction, all known pending gaps between its first/highest observations are finalized. The eviction raises a precision-limit warning; future observations for that source start a new baseline and cannot infer losses during the discarded-history interval. Missing totals saturate rather than wrapping uint64. Local record ordinals never feed this analysis.

## Actual verification

Owner build directory: `build/session-owner`. Matching toolchain/package configuration:

```text
cmake -S . -B build/session-owner -G Ninja
  -DCMAKE_MAKE_PROGRAM=C:/Qt/Tools/Ninja/ninja.exe
  -DCMAKE_CXX_COMPILER=C:/Qt/Tools/mingw1310_64/bin/g++.exe
  -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/mingw_64
  -DQt6SerialPort_DIR=C:/Users/threeTeeth/workSpace/github/PortBridge/.deps/qtserialport-install/lib/cmake/Qt6SerialPort
  -DCMAKE_BUILD_TYPE=Release
cmake --build build/session-owner --target test_session -j 4
```

`test_session.exe` was run with Qt, MinGW and `.deps/qtserialport-install/bin` on runtime PATH, writing Qt Test text output to `build/session-owner/session-test.txt`. The final complete owner run reported **19 passed, 0 failed, 0 skipped, 38,879 ms**. This includes 17 substantive test slots plus Qt Test initialization/cleanup. The final compilation emitted no session warnings.

Coverage includes strict codec errors/UTF-8/EOL, Chinese profile/command paths, corrupt/invalid imports and safe replacement, oversized JSON preserving the prior file, finite periodic sends and explicit stop/client removal, capture reconstruction/rotation/truncation/header/max-length rejection, incomplete capture export, queue capacity including retained vector capacity, finite/unlimited duration, real catalog-write failure preventing further rotation, 1,028 rotations retaining 1,024 catalog entries while preserving all 1,028 files, bounded background reload, progress/cancellation preserving output, paused/cleared/bounded display, default/explicit sequence policy, endian/offset/2^60 gap analysis, receive-only UDP resolution error, serial open failure, and SYSTEM events without traffic-counter changes.

The TCP functional capture test reconstructs all **1,048,580 bytes** with a 70,000-byte recording queue, verifies public connection IDs, zero application drops/recording failures, and a bounded queue high-water. It switches a live same-transport session while its old peer still sends, verifies no old bytes enter new-session statistics/display, and confirms new public client identity.

The publication regression uses the independent reviewer's observer behavior: 100 admitted rotated records, background reads of durable metadata only after published completeness, no complete snapshot paired with an incomplete sidecar, all 100 recorded, zero induced catalog/write failures. Wrap `[UINT64_MAX-1, UINT64_MAX, 0, 2]` finalizes exactly one missing value with zero false reordering. The 65-source `[100,102]` regression finalizes all 65 known gaps, including the evicted source.

No physical serial loopback/unplug, long-running serial test, real two-machine 2.5G TCP/UDP link, sustained disk recording throughput, clean-system deployment or multi-GB export throughput has been verified. Functional loopback tests and bounded format/queue tests do not establish those hardware/performance results. Coordinator-owned integrated build/UI review and those external acceptance conditions remain outside this owner's evidence.
