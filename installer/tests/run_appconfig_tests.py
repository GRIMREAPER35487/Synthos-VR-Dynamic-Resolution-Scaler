"""Runs the installer's appconfig.json editing logic (steamvr_appconfig.iss) against sample files.

Usage: python installer/tests/run_appconfig_tests.py
Requires Inno Setup 6 (ISCC.exe). Never touches the real Steam appconfig.json.
"""
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ENTRY = r"C:\Test Dir\manifest.vrmanifest"
A = r"C:\Games\a.vrmanifest"
B = r"C:\Games [VR]\b.vrmanifest"  # brackets inside a path string


def pretty(paths):
    return json.dumps({"manifest_paths": paths, "other": [1]}, indent=2)


def inline(paths):
    return json.dumps({"manifest_paths": paths, "other": [1]})


# name -> (input text, expected manifest_paths afterwards)
CASES = {
    "add_empty.json": ('{\n  "manifest_paths" : [\n  ],\n  "other": [1]\n}', [ENTRY]),
    "add_multiline.json": (pretty([A]), [A, ENTRY]),
    "add_inline.json": (inline([A]), [A, ENTRY]),
    "add_bracket_in_path.json": (pretty([B]), [B, ENTRY]),
    "add_already_present.json": (pretty([A, ENTRY]), [A, ENTRY]),
    "add_no_key.json": ('{\n  "other": [1]\n}', None),
    "remove_last.json": (pretty([A, ENTRY]), [A]),
    "remove_first.json": (pretty([ENTRY, A]), [A]),
    "remove_middle.json": (pretty([A, ENTRY, B]), [A, B]),
    "remove_only.json": (pretty([ENTRY]), []),
    "roundtrip.json": (pretty([A, B]), [A, B]),
}


def find_iscc():
    for p in (
        Path(os.environ.get("LOCALAPPDATA", "")) / "Programs/Inno Setup 6/ISCC.exe",
        Path(r"C:\Program Files (x86)\Inno Setup 6\ISCC.exe"),
        Path(r"C:\Program Files\Inno Setup 6\ISCC.exe"),
    ):
        if p.exists():
            return p
    return shutil.which("ISCC")


def main():
    iscc = find_iscc()
    if not iscc:
        sys.exit("ISCC.exe (Inno Setup 6) not found")

    work = Path(tempfile.mkdtemp(prefix="appconfig_test_"))
    fixtures = work / "fixtures"
    fixtures.mkdir()
    for name, (text, _) in CASES.items():
        (fixtures / name).write_text(text, encoding="utf-8")

    subprocess.run([str(iscc), "/Q", f"/O{work}", str(HERE / "test_appconfig.iss")], check=True)
    subprocess.run([str(work / "test_appconfig.exe"), "/VERYSILENT", f"/fixtures={fixtures}"])

    failures = 0
    for name, (original, expected) in CASES.items():
        text = (fixtures / name).read_text(encoding="utf-8")
        try:
            data = json.loads(text)
        except json.JSONDecodeError as e:
            print(f"FAIL {name}: invalid JSON ({e})\n{text}\n")
            failures += 1
            continue
        if expected is None:
            ok = text == original
        else:
            ok = data.get("manifest_paths") == expected and data.get("other") == [1]
        print(f"{'ok  ' if ok else 'FAIL'} {name}")
        if not ok:
            print(text + "\n")
            failures += 1

    shutil.rmtree(work, ignore_errors=True)
    print(f"\n{len(CASES) - failures}/{len(CASES)} passed")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
