# Dependency provenance

The pinned sources below are downloaded by `scripts/bootstrap.py` into the
ignored `third_party/` and `.tools/` directories. Registry components are
restored by ESP-IDF using the tracked manifests, `dependencies.lock` and
`scripts/component-constraints.txt`. SDKs, toolchains and dependency Git history
are not distributed with this source snapshot.

| Dependency | Commit |
| --- | --- |
| coin0/esp-webrtc-solution, dev/agora-demo | b57d626c9a885c75a216a5b2deccc6bc2fe2e045 |
| esp-mosaico/esp-mosaico-bsp | a7a93d290be28d18fbfd32361c90aa20b0cd1c67 |
| espressif/esp-idf, Mosaico pinned SDK | 7b9cc1ac79f865983f59bb8ff3ff43eb74ff1dbe |

`main/agora_auth.c`, `main/agora_auth.h`, `main/agora_signaling.c`, and
`main/agora_signaling.h` derive from the specified Agora demo. The authorization
adapter preserves the gateway's `stringuid` parameter and uses the local RTC
UID returned by the MyBot Device API.
See the upstream repository's license. WebRTC, rendering, BSP and managed
components retain their respective upstream licenses.

`main/pair_code_font.c` contains only Montserrat Medium digits at 100 px,
generated with `lv_font_conv` 1.5.3 from LVGL's bundled
`scripts/built_in_font/Montserrat-Medium.ttf`. The generation options are recorded
in the file header; the SIL Open Font License 1.1 is included in
`licenses/Montserrat-OFL.txt`.

One integration patch changes av_render's esp_codec_dev requirement from
`^2.0.0-beta1` to `~1.6`, matching the BSP and capture package. The used I2S render
API is available in that series. The bootstrap applies this exact manifest change after downloading the
pinned source.

The Mosaico BSP enables the ES8311 DAC reference (`no_dac_ref=false`) so the
application can capture paired microphone/reference channels for software AEC.
The bootstrap reproduces the exact board-local change, including its
microphone/reference channel-order comment. The existing firmware and AEC
routing code are retained.

`main/CMakeLists.txt` also namespaces the UAC component's four lifecycle callbacks
with private compiler definitions. Application callbacks call these original
handlers before updating session/USB state, retaining UAC's stream reset logic
without editing the managed component.

