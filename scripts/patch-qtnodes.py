"""Apply the pinned QtNodes patch with exact source hashes and atomic writes."""
import argparse
import hashlib
import json
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("source", type=Path)
args = parser.parse_args()
manifest = json.loads((Path(__file__).parent.parent / "patches/qtnodes-qvariant-const.json").read_text(encoding="utf-8"))
for entry in manifest["files"]:
    target = args.source / entry["path"]
    original = target.read_bytes()
    digest = hashlib.sha256(original).hexdigest()
    if digest == entry["afterSha256"]:
        continue
    if digest != entry["beforeSha256"]:
        raise SystemExit(f"Unexpected QtNodes source; preserved: {target}")
    text = original.decode("utf-8")
    # Match all edits against the original, then apply from the bottom upward.
    edits = []
    for edit in entry["edits"]:
        if text.count(edit["old"]) != 1:
            raise SystemExit(f"Non-unique patch context: {target}")
        start = text.index(edit["old"])
        edits.append((start, start + len(edit["old"]), edit["new"]))
    for start, end, replacement in sorted(edits, reverse=True):
        text = text[:start] + replacement + text[end:]
    result = text.encode("utf-8")
    if hashlib.sha256(result).hexdigest() != entry["afterSha256"]:
        raise SystemExit(f"Patch result hash mismatch: {target}")
    temporary = target.with_suffix(target.suffix + ".portbridge.tmp")
    temporary.write_bytes(result)
    temporary.replace(target)
print("QtNodes const QVariant extraction patch verified.")
