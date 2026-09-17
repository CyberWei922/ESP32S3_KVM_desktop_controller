#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
output_file="${TMPDIR:-/tmp}/statsforkvm_protocol_test"

cc -std=c11 -Wall -Wextra -Werror \
  -I"$project_dir/main/include" \
  -I"$project_dir/managed_components/espressif__cjson/cJSON" \
  "$project_dir/main/protocol.c" \
  "$project_dir/managed_components/espressif__cjson/cJSON/cJSON.c" \
  "$project_dir/test/protocol_test.c" \
  -lm -o "$output_file"
"$output_file"
