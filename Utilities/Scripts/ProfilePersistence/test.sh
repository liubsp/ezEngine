#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
build="$root/Workspace/profile-persistence"
output="$root/Workspace/profile-persistence-output"
results="$build/results"
mkdir -p "$results"

cmake -S "$root" -B "$build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DEZ_COMPILE_ENGINE_AS_DLL=ON \
  -DEZ_OUTPUT_DIRECTORY_DLL="$output/Bin" \
  -DEZ_OUTPUT_DIRECTORY_LIB="$output/Lib" \
  -DEZ_ENABLE_QT_SUPPORT=OFF -DEZ_BUILD_VULKAN=OFF -DEZ_BUILD_OPENXR=OFF \
  -DEZ_BUILD_SAMPLES=OFF -DEZ_BUILD_UNITTESTS=ON \
  -DEZ_3RDPARTY_GLFW_SUPPORT=ON -DGLFW_USE_WAYLAND=OFF -DGLFW_USE_OSMESA=OFF \
  -DEZ_BUILD_RMLUI=OFF \
  -DEZ_3RDPARTY_TRACELOGGING_LTTNG_SUPPORT=OFF
cmake --build "$build" --target FoundationTest CoreTest --parallel 2

mapfile -t core_tests < <(find "$output/Bin" -type f -name CoreTest)
mapfile -t foundation_tests < <(find "$output/Bin" -type f -name FoundationTest)
test "${#core_tests[@]}" -eq 1
test "${#foundation_tests[@]}" -eq 1
core="${core_tests[0]}"
foundation="${foundation_tests[0]}"

run_test() {
  local executable="$1" filter="$2" label="$3"
  mkdir -p "$results/$label"
  timeout --kill-after=5s 120s "$executable" -noGui -filter "$filter" \
    -outputDir "$results/$label" -json "$results/$label/results.json" \
    2>&1 | tee "$results/$label/stdout.log"
}

# First require the real child fixture and normal format/loader path to pass unmodified.
run_test "$core" PlatformProfile profiles-before

# Negative control: remove only the libc drain in this disposable build, not the checked
# serializer or folder writer. Restore the source even if the build or assertion check fails.
source_file="$root/Code/Engine/Foundation/Platform/Posix/OSFile_Posix.inl"
backup="$build/OSFile_Posix.inl.before-negative-control"
cp "$source_file" "$backup"
trap 'cp "$backup" "$source_file"; rm -f "$backup"' EXIT
python3 - "$source_file" <<'PY'
import pathlib
import sys

path = pathlib.Path(sys.argv[1])
source = path.read_bytes()
drain = b"return fflush(m_FileData.m_pFileHandle) == 0 ? EZ_SUCCESS : EZ_FAILURE;"
if source.count(drain) != 1:
    raise SystemExit("Expected exactly one POSIX write drain for the negative control")
path.write_bytes(source.replace(drain, b"return EZ_SUCCESS;"))
PY
cmake --build "$build" --target Foundation --parallel 2
mkdir -p "$results/negative-control"
set +e
# This invocation is itself the isolated child: its limits never affect this script.
timeout --kill-after=5s 120s "$core" -noGui -filter PlatformProfilePosixFailure \
  -profileWriteFailureChild -outputDir "$results/negative-control" \
  -json "$results/negative-control/results.json" > "$results/negative-control/stdout.log" 2>&1
status=$?
set -e
cat "$results/negative-control/stdout.log"
cp "$backup" "$source_file"
rm "$backup"
trap - EXIT
cmake --build "$build" --target Foundation --parallel 2

# An open/setup failure, crash, timeout or additional failed assertion is not the expected red.
test "$status" -eq 1
grep -Fq 'Test failed: saveResult.Failed()' "$results/negative-control/stdout.log"
test "$(grep -c 'Test failed:' "$results/negative-control/stdout.log")" -eq 1

run_test "$core" PlatformProfile profiles-after
run_test "$foundation" OSFile os-file
run_test "$foundation" FileSystem file-system
