#!/usr/bin/env bash
set -euo pipefail
project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
test_build="$(mktemp -d "${TMPDIR:-/tmp}/mosaico-aec-test.XXXXXX")"
trap 'rm -rf "$test_build"' EXIT
cd "$project_root"
test_cc="${CC:-clang}"
flags=(-std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined -fno-omit-frame-pointer
    -Itests/host/stubs -Imain
    -Imanaged_components/espressif__esp-sr/include/esp32s31
    -Imanaged_components/espressif__esp_audio_effects/include
    -Imanaged_components/espressif__esp_codec_dev/include
    -Imanaged_components/espressif__esp_codec_dev/interface
    -Imanaged_components/espressif__esp_capture/include
    -Imanaged_components/espressif__esp_capture/interface)
for bypass in 0 1; do
    "$test_cc" "${flags[@]}" -DCONFIG_MYBOT_AEC_BYPASS="$bypass" -Dfree=host_free \
        -c main/aec_processor.c -o "$test_build/aec.o"
    "$test_cc" "${flags[@]}" -DCONFIG_MYBOT_AEC_BYPASS="$bypass" \
        tests/host/test_aec.c main/usb_capture.c main/pcm_ring.c "$test_build/aec.o" \
        -lm -o "$test_build/test_aec"
    for chunk in 160 256 512; do
        "$test_build/test_aec" "$chunk"
    done
    for step in {1..11}; do
        "$test_build/test_aec" 256 "$step"
    done
    "$test_build/test_aec" 513
done
echo 'PASS: paired AEC/capture routing, continuity, resets and errors (ASan/UBSan, normal and bypass).'
