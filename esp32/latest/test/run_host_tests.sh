#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
test_dir="$(mktemp -d "${TMPDIR:-/tmp}/kvm-latest-tests.XXXXXX")"
trap 'rm -rf "$test_dir"' EXIT

cc -std=c11 -Wall -Wextra -Werror \
  -I"$project_dir/main/include" \
  -I"$project_dir/managed_components/espressif__cjson/cJSON" \
  "$project_dir/main/protocol.c" \
  "$project_dir/managed_components/espressif__cjson/cJSON/cJSON.c" \
  "$project_dir/test/protocol_test.c" \
  -lm -o "$test_dir/protocol_test"
"$test_dir/protocol_test"

cc -std=c11 -Wall -Wextra -Werror \
  -I"$project_dir/main/include" \
  "$project_dir/main/button_gesture.c" \
  "$project_dir/test/button_gesture_test.c" \
  -o "$test_dir/button_gesture_test"
"$test_dir/button_gesture_test"
