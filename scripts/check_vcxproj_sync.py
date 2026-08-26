#!/usr/bin/env python3
"""
check_vcxproj_sync.py - verify src/CMakeLists.txt and src/OpenXcom.2010.vcxproj stay in sync.

Both files list the same compile sources: all src/**/*.cpp + libs/miniz/miniz.c + libs/rapidyaml/**/*.cpp
relative to repo root as src/... and libs/...

Usage:
  python scripts/check_vcxproj_sync.py
  python scripts/check_vcxproj_sync.py --verbose

Exit 0 = in sync, 1 = drift detected.
"""
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
CMAKE_PATH = REPO_ROOT / "src" / "CMakeLists.txt"
VCXPROJ_PATH = REPO_ROOT / "src" / "OpenXcom.2010.vcxproj"

# Only these extensions are compiled (ClCompile)
COMPILE_EXTS = (".cpp", ".c")

def parse_cmake():
    text = CMAKE_PATH.read_text(encoding="utf-8", errors="ignore")
    files = set()
    # Find every set(<name>_src ... ) block - includes root_src, basescape_src, etc. + rapidyaml_src + c_src
    # Also picks up rapidyaml_src and c_src (they also end with _src)
    for m in re.finditer(r'set\s*\(\s*(\w+_src)\s*(.*?)\)', text, re.DOTALL | re.IGNORECASE):
        block = m.group(2)
        # Extract tokens that look like paths ending with .cpp or .c
        for tok in re.findall(r'[\w/\.\-\\]+\.(?:cpp|c)\b', block):
            tok = tok.strip().replace("\\", "/")
            # Normalize to repo-relative
            if tok.startswith("../"):
                repo_rel = tok[3:].lstrip("/")
            elif tok.startswith("libs/"):
                repo_rel = tok
            else:
                # src-relative (e.g., Basescape/Foo.cpp or lodepng.cpp)
                repo_rel = f"src/{tok}"
            files.add(repo_rel)
    # Filter to compile ext only (defensive)
    files = {f for f in files if f.lower().endswith(COMPILE_EXTS)}
    return files

def parse_vcxproj():
    text = VCXPROJ_PATH.read_text(encoding="utf-8", errors="ignore")
    files = set()
    # ClCompile Include="..."
    for m in re.finditer(r'<ClCompile\s+Include="([^"]+)"', text, re.IGNORECASE):
        inc = m.group(1).strip().replace("\\", "/")
        if inc.startswith("../"):
            repo_rel = inc[3:].lstrip("/")
        elif inc.startswith("libs/"):
            repo_rel = inc
        else:
            repo_rel = f"src/{inc}"
        # Only keep compile sources
        if repo_rel.lower().endswith(COMPILE_EXTS):
            files.add(repo_rel)
    return files

def main():
    verbose = "--verbose" in sys.argv or "-v" in sys.argv
    if not CMAKE_PATH.exists():
        print(f"ERROR: missing {CMAKE_PATH}", file=sys.stderr)
        return 2
    if not VCXPROJ_PATH.exists():
        print(f"ERROR: missing {VCXPROJ_PATH}", file=sys.stderr)
        return 2

    cmake_files = parse_cmake()
    vcx_files = parse_vcxproj()

    if verbose:
        print(f"CMake  : {len(cmake_files)} compile files")
        print(f"VCXProj: {len(vcx_files)} compile files")

    only_cmake = sorted(cmake_files - vcx_files)
    only_vcx = sorted(vcx_files - cmake_files)

    lower_cmake = {f.lower(): f for f in cmake_files}
    lower_vcx = {f.lower(): f for f in vcx_files}

    if not only_cmake and not only_vcx:
        print("OK: src/CMakeLists.txt and src/OpenXcom.2010.vcxproj are in sync.")
        print(f"  {len(cmake_files)} compile files checked.")
        return 0

    print("DRIFT: src/CMakeLists.txt and src/OpenXcom.2010.vcxproj differ!", file=sys.stderr)
    print(file=sys.stderr)

    if only_cmake:
        print(f"Only in CMake ({len(only_cmake)}):", file=sys.stderr)
        for f in only_cmake:
            # hint if case mismatch
            hint = ""
            if f.lower() in lower_vcx:
                hint = f"  (case mismatch -> vcxproj has '{lower_vcx[f.lower()]}')"
            print(f"  + {f}{hint}", file=sys.stderr)
        print(file=sys.stderr)

    if only_vcx:
        print(f"Only in vcxproj ({len(only_vcx)}):", file=sys.stderr)
        for f in only_vcx:
            hint = ""
            if f.lower() in lower_cmake:
                hint = f"  (case mismatch -> CMake has '{lower_cmake[f.lower()]}')"
            print(f"  - {f}{hint}", file=sys.stderr)
        print(file=sys.stderr)

    print("Fix: add/remove the file in BOTH src/CMakeLists.txt AND src/OpenXcom.2010.vcxproj", file=sys.stderr)
    print("     (and check src/OpenXcom.2010.vcxproj.filters if you want IDE grouping)", file=sys.stderr)
    return 1

if __name__ == "__main__":
    sys.exit(main())
