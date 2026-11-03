#!/usr/bin/env bash
set -euo pipefail
project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
ui_build="$project_root/build-ui-host"
cmake -S "$project_root/tests/ui" -B "$ui_build" -DCMAKE_BUILD_TYPE=Debug
cmake --build "$ui_build" --parallel 8
mkdir -p "$ui_build/previews"
"$ui_build/power_tests"
"$ui_build/ui_preview" "$ui_build/previews"
"$ui_build/ui_preview" --button-unavailable
python3 "$project_root/tests/ui/export_previews.py" "$ui_build/previews"
