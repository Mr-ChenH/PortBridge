"""Verify every saved PBCAP001 payload against the local acceptance sender.

Streams records across rotated files, including TCP read fragmentation. This
verifies the binary files rather than relying on live byte counters.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct


def verify(directory: Path, protocol: str, expected_frames: int) -> dict:
    frame_size = 1472 if protocol == "udp" else 4096
    base = bytes((position * 17) & 255 for position in range(frame_size))
    translations = [bytes(value ^ mask for value in range(256)) for mask in range(256)]
    pending = bytearray()
    frame_number = 0
    payload_bytes = 0
    saved_records = 0
    files = []
    for path in sorted(directory.glob("*.pbc")):
        digest = hashlib.sha256()
        file_records = file_payload_bytes = 0
        footer_seen = False
        with path.open("rb") as capture:
            def read(amount: int) -> bytes:
                value = capture.read(amount)
                assert len(value) == amount, f"Truncated capture: {path.name}"
                digest.update(value)
                return value

            assert read(24)[:16] == b"PBCAP001" + struct.pack("<II", 1, 24)
            while tag := capture.read(8):
                assert len(tag) == 8, "Truncated record tag"
                digest.update(tag)
                length = struct.unpack_from("<I", tag, 4)[0]
                if tag[:4] == b"DONE":
                    assert length == 24
                    count, size, complete = struct.unpack("<QQB7x", read(24))
                    assert (count, size, complete) == (file_records, file_payload_bytes, 1)
                    assert capture.read(1) == b"", "Bytes after footer"
                    footer_seen = True
                    break
                assert tag[:4] == b"DATA" and 44 <= length <= 44 + 4096 + 64 * 1024 * 1024
                fixed = read(44)
                _, _, _, transport, direction, reserved, peer_length, _, reserved2, size = struct.unpack("<QQQBBHIHHQ", fixed)
                assert not reserved and not reserved2 and peer_length <= 4096
                assert length == 44 + peer_length + size
                assert transport == (3 if protocol == "udp" else 2)
                assert direction == 0, "Unexpected TX or SYSTEM record"
                read(peer_length).decode("utf-8")
                payload = read(size)
                file_records += 1
                file_payload_bytes += size
                saved_records += 1
                payload_bytes += size
                if protocol == "udp":
                    assert size == frame_size, "UDP boundary changed"
                pending.extend(payload)
                while len(pending) >= frame_size:
                    expected = bytearray(base.translate(translations[(frame_number * 31) & 255]))
                    expected[:8] = struct.pack(">Q", frame_number)
                    assert pending[:frame_size] == expected, f"Payload corruption or missing frame at {frame_number}"
                    del pending[:frame_size]
                    frame_number += 1
        assert footer_seen, f"No completion footer: {path.name}"
        metadata = json.loads(Path(str(path) + ".meta.json").read_text(encoding="utf-8"))
        assert metadata["complete"] is True and not metadata["error"]
        assert int(metadata["records"]) == file_records and int(metadata["bytes"]) == file_payload_bytes
        files.append({"file": path.name, "bytes": path.stat().st_size, "payloadBytes": file_payload_bytes,
                      "records": file_records, "sha256": digest.hexdigest()})
    assert files and not pending and frame_number == expected_frames
    assert payload_bytes == frame_size * expected_frames
    return {"state": "passed", "protocol": protocol, "verifiedFrames": frame_number,
            "payloadBytes": payload_bytes, "savedRecords": saved_records,
            "allPayloadBytesMatch": True, "allFootersAndMetadataComplete": True, "files": files}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--capture-directory", type=Path, required=True)
    parser.add_argument("--protocol", choices=["tcp", "udp"], required=True)
    parser.add_argument("--expected-frames", type=int, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        result = verify(args.capture_directory, args.protocol, args.expected_frames)
    except (AssertionError, OSError, ValueError, KeyError) as error:
        result = {"state": "failed", "error": str(error)}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({key: value for key, value in result.items() if key != "files"}, ensure_ascii=False))
    raise SystemExit(result["state"] != "passed")
