# Independent network and benchmark review

Review date: 2026-10-07. Outcome: **NETWORK-1, the confirmed P1 retained-pool bounded-memory defect, is fixed and independently reverified; no remaining concrete network/benchmark correctness blocker was found**. All eight final network groups and the actual socket/allocation regression pass. This is a source and Windows localhost functional review, not physical-link or sustained-performance acceptance.

## Scope and provenance

Read the actual final `src/network/network_engine.cpp`, private `src/network/buffer_pool.hpp`, `include/portbridge/network_engine.hpp`, `include/portbridge/types.hpp`, `tests/test_network.cpp`, `tools/network_bench.cpp`, the network implementation report, the high-throughput requirements, and both `docs/validation/localhost-*.json` artifacts. Independently built and executed the final network tests and benchmark from the isolated harness under `build/ui-independent`. No product, shared header, root CMake, owner test, or stored validation artifact was edited by this reviewer; no agents or commits were created.

The source and validation artifact hashes before and after each independent execution phase were identical. Initial manifests: `build/ui-independent/network-hashes-before.json` and `network-hashes-after.json`; **final post-fix manifests**: `network-hashes-before-final.json` and `network-hashes-after-final.json`. The coordinator repaired NETWORK-1 between those phases and regenerated the stored localhost baselines. The table below names the final reviewed inputs; the old source hash is retained with the defect reproduction.

| File | SHA-256 |
| --- | --- |
| `src/network/network_engine.cpp` | `3e3f550af9d7be5e83d64e90defe2041029a0302331eb0632f2e2e8282e3b673` |
| `src/network/buffer_pool.hpp` | `fe0e9ff06c158271aabb7f7dc943b710d201c6ae6fa6d76b30f4f470b6068bd9` |
| `include/portbridge/network_engine.hpp` | `1428c28956f023118a8ee0944fee10a7cd2d8f6de3799a1a454ba37ace5fe6b3` |
| `include/portbridge/types.hpp` | `714265b99e99b444e102543d366beac532279a67636af8229b90cfac52435d0d` |
| `tests/test_network.cpp` | `b721a9b113031d1e7673a8a0ddacc894bcca617bec9c2dabb8841212ccdeb4df` |
| `tools/network_bench.cpp` | `7dcc7c42b55e353c652914119fcf53345e66a8388fb62df119712143abec596b` |
| `docs/validation/localhost-tcp.json` | `4e1d360f56cc697006be81e4e021a988162de6e3681b2deb1e640690ec5c9310` |
| `docs/validation/localhost-udp.json` | `8fb15bc4fdcbd7e83b98861cdaf517ad79627e9afda5147d7387cf4f036835b3` |

## Independent build and execution

Toolchain: Qt 6.8.3 at `C:/Qt/6.8.3/mingw_64`, GCC 13.1.0 at `C:/Qt/Tools/mingw1310_64/bin/g++.exe`, Ninja at `C:/Qt/Tools/Ninja/ninja.exe`, native Windows. The harness includes the real root project as an excluded subdirectory, so these are the product's targets and source files. Network targets themselves do not link Qt.

```sh
cmake -S build/ui-independent -B build/ui-independent/out -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_COMPILER=C:/Qt/Tools/mingw1310_64/bin/g++.exe \
  -DCMAKE_MAKE_PROGRAM=C:/Qt/Tools/Ninja/ninja.exe \
  '-DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/mingw_64;C:/Users/threeTeeth/workSpace/github/PortBridge/.deps/qtserialport-install' \
  -DQt6SerialPort_DIR=C:/Users/threeTeeth/workSpace/github/PortBridge/.deps/qtserialport-install/lib/cmake/Qt6SerialPort
cmake --build build/ui-independent/out --target test_network portbridge_bench -j 4
python build/ui-independent/review_network.py -final
```

The independent Python driver adds the matching MinGW runtime to Windows PATH, launches real executables, checks JSON/count invariants, and injects actual UDP test frames through Python sockets. It preserves stdout, stderr, return codes, command arguments, and before/after hashes. Final evidence: `build/ui-independent/network-tests-final.txt`, `network-independent-results-final.json`, `independent-*.stdout-final.json`, and `independent-*.stderr-final.txt`. Initial results remain in the unsuffixed files.

All **eight final** test groups pass:

1. TCP arbitrary splits and repeated retained-block backpressure; exact 180,123-byte reconstruction, stable shared payload/timestamp/ordinal, actual buffers, completed TX.
2. Four TCP clients, endpoint-matched IDs, directed/broadcast sends, and selective disconnect.
3. Empty and maximum legal UDP datagrams, multiple origins, small retained capacities, empty TX, oversize rejection, and continued receive after consumer drops.
4. Native Windows absent UDP peer: ICMP produces an error notification with endpoints while the socket stays bound; another source is received, two TX completions total 77 bytes, and reservations drain.
5. Capacity-based bounded admission, 10,000 overflow attempts, and no retransmission when a completed-TX consumer rejects.
6. An old backpressured TCP block cannot be delivered after replacement with a UDP generation.
7. Cancel/deadline/refusal, repeated start/stop, rapid generation changes, and callback quiescence after destruction.
8. The new private-pool regression proves no reuse while another consumer holds a buffer, reuse after release, expiration of a pool with a populated 2 MiB free cache, and validity of its surviving one-byte payload.

## Short genuine benchmark cases

These runs exercise functionality with real localhost sockets. Throughput is provided as run evidence only; the short duration and active concurrent workspace make it unsuitable as a performance acceptance claim.

| Case | Configuration | Completed TX / unique RX | Bytes RX | Rejections | Outcome |
| --- | --- | --- | --- | --- | --- |
| UDP self-loop | 1,472 B; 1,000 frames/s; 0.3 s | 300 / 300 | 441,600 | 155 | exit 0, zero missing/invalid, drained |
| TCP self-loop | 4,096 B; 1,000 frames/s; 0.3 s | 300 / 300 | 1,228,800 | 0 | exit 0, zero missing/invalid, drained |
| TCP queue pressure | 4,096 B; unpaced; 0.2 s; 4,096 B queue | 5,514 / 5,514 | 22,585,344 | 378,104 | exit 0, zero missing/invalid; peak pending exactly 4,096 B |

Rejections are admission attempts, including retries; they are not completed-transmission loss. UDP destination resolution occurs after Bound, so early admission rejection can also occur without queue saturation. Each successful case reconciles `tx_completed_bytes == rx_bytes == tx_completed_frames * payload_bytes` and completed TX frames equal unique RX frames.

Two independent receive-mode injections check that diagnostics derive from received bytes:

- Six 64-byte UDP datagrams carry sequences `0,2,1,2,4` plus sequence 3 with a deliberately corrupted payload. With `--expected 5`, result is six received datagrams, five valid frames, four unique frames, one duplicate, one reordered frame, one missing frame, one checksum/format failure, loss fraction 0.2, and exit **2** with `success:false`.
- Sequences `0,65537,1` cross the 65,536-position observation window. Result is three valid received frames, two classified unique frames, one late frame outside the window, `loss_count_exact:false`, and `loss_fraction:null`. The reported missing count is therefore an estimate, not an exact loss assertion. UDP observation loss alone leaves execution successful.

## Stored final localhost artifacts

Read and hashed both artifacts rather than treating their filenames as evidence of hardware:

| Artifact | Completed TX / unique RX | TX bytes / RX bytes | Missing / checksum failures | Reported RX bytes/s |
| --- | --- | --- | --- | --- |
| `docs/validation/localhost-tcp.json` | 10,000 / 10,000 | 163,840,000 / 163,840,000 | 0 / 0 | 81,331,303.691058 |
| `docs/validation/localhost-udp.json` | 40,000 / 40,000 | 58,880,000 / 58,880,000 | 0 / 0 | 29,209,523.713614 |

Their arithmetic is internally consistent with the payload sizes and reported elapsed times, both have zero pending bytes at stop, and both explicitly say localhost, `physical_2_5g_validated:false`, recording/UI disabled, and `kernel_or_nic_drops:null`. Their full workloads were not independently repeated; the short independent cases above are the reviewer-generated execution evidence.

## NETWORK-1 — P1, resolved: a tiny retained payload kept an entire retired pool cache alive

The coordinator raised the ownership concern after the initial passing transport review. A focused actual-socket/allocation reproducer independently confirms it on source hash `389193183c5448a673639f1992b90089e14437fbb9cea02ba6c42d27b14b7fd3`. `BufferPool::acquire` captures a strong `shared_from_this()` in every payload's custom deleter. One surviving tiny payload therefore retains the pool's unrelated free buckets after its NetworkEngine is destroyed.

`build/ui-independent/pool_lifetime.cpp` intercepts ordinary C++ allocations to count live allocations of exactly 65,536 bytes. Each generation receives 48×65,536 real TCP bytes, holds enough blocks to warm the large-buffer bucket, receives one additional byte (repacked to capacity 256), releases every bulk block, retains only that tiny payload, and fully destroys/joins the engine. It repeats six times, then releases the six tiny payloads. It does not modify or copy the network implementation.

Actual output in `build/ui-independent/pool-lifetime-before.txt`:

| Destroyed engines | Retained tiny payload capacity charged | Live retired 65,536-byte allocations | Retained large bytes |
| --- | --- | --- | --- |
| 1 | 256 B | 32 | 2,097,152 |
| 2 | 512 B | 64 | 4,194,304 |
| 3 | 768 B | 96 | 6,291,456 |
| 4 | 1,024 B | 128 | 8,388,608 |
| 5 | 1,280 B | 160 | 10,485,760 |
| 6 | 1,536 B | 192 | 12,582,912 |
| After releasing tiny payloads | 0 B | 0 | 0 |

This counts allocations, not process RSS, and isolates the large size class; other cached size classes could add more. `SessionController::start` retires/recreates network engines without clearing retained samples. The UI's ordinary same-profile disconnect/reconnect also retains RecordModel rows. Before the correction, its 10 MiB budget, which charges each payload's capacity and metadata, did not cover these old engine caches; even six tiny rows could pin more than the whole display budget. The four-engine retirement bound did not help once destruction had completed and the pool was retained solely by payload deleters.

The finding was escalated before completion; no product patch was made by this reviewer. A weak pool reference in the deleter is the narrow ownership correction: return buffers to a live pool, otherwise delete the released buffer while letting retired free buckets disappear immediately. Verification must show both cache destruction and continued validity of surviving payload bytes.

```sh
cmake --build build/ui-independent/out --target pool_lifetime -j 4
# Run with C:/Qt/Tools/mingw1310_64/bin on Windows PATH.
# Original-source reproduction (expects the demonstrated retained-cache defect):
build/ui-independent/out/pool_lifetime.exe
# Final-source verification (expects cache destruction and valid retained bytes):
build/ui-independent/out/pool_lifetime.exe --expect-fixed
```

Final correction, authored by the coordinator, extracts the unchanged size classes and 32-entry limits into private `src/network/buffer_pool.hpp` and makes the payload deleter capture a weak pool reference. A released buffer returns to a still-live pool; otherwise its own bytes are deleted without retaining unrelated caches. No public API or transport logic changed.

Independent post-fix evidence: `build/ui-independent/pool-lifetime-after.txt`. Rebuilt `pool_lifetime` against the final real network library and ran `pool_lifetime.exe --expect-fixed`. After each of all six actual TCP generations, live 65,536-byte cache allocations are **zero**, while all retained one-byte payloads still equal `x`; releasing them remains safe. This closes NETWORK-1. The permanent lifetime/reuse group and the seven transport groups also pass independently. Final network/source hashes stayed stable through these runs. The broader final manifest is `build/ui-independent/hashes-final.json`.

## Source conclusions and limits

Admission is reserved before posting; retained vector capacity and broadcast multiplicity are charged, and a 4,096-write cap and coalesced rejection events bound descriptors/notifications. TCP false receive admission holds one unchanged block and pauses further reads; UDP false receive admission continues reading and delegates drop accounting to the consumer. Write completion accounts actual TCP prefixes and complete UDP datagrams, and consumer rejection cannot resend already completed TX. Generation checks cover resolver, timers, accept/read/write completions, retry timers, posted start/stop, and selective disconnect. Tests verify these paths rather than relying on source inspection alone.

The benchmark bounds sequence identities to 256 and each identity to 65,536 slots; TCP assembly is per active connection and cleared on client removal. Frames check magic, declared length, FNV checksum, deterministic contents, and bounded sequence range. Expected trailing loss is known for self-loop or a receive-mode sender count supplied via `--expected`; older unclassified frames explicitly disable exact-loss reporting. A supplied expected count assumes a reconciled sender manifest, as documented by the tool. Kernel/NIC attribution remains unknown rather than zero.

Physical serial behavior, two-machine or physical 2.5G throughput, high-PPS sustained/burst envelopes, RSS under long-lived adversarial traffic, sustained disk capture, clean-system deployment, and hardware latency were not validated here. Engine destruction joins its I/O thread; a blocked consumer or OS DNS resolver can delay that join, as the implementation report states. No unmeasured performance or hardware capability is certified by this review.
