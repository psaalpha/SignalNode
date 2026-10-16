#!/usr/bin/env bash
set -eu
project_dir="$(cd "$(dirname "$0")/.." && pwd)"
cmake -S "$project_dir" -B "$project_dir/build-host" -DCMAKE_BUILD_TYPE=Debug
cmake --build "$project_dir/build-host" --parallel 2
ctest --test-dir "$project_dir/build-host" --output-on-failure
cmake -S "$project_dir" -B "$project_dir/build-sanitize" -DCMAKE_BUILD_TYPE=Debug -DSN_SANITIZE=ON
cmake --build "$project_dir/build-sanitize" --parallel 2
ctest --test-dir "$project_dir/build-sanitize" --output-on-failure
