# Mosaico MyBot demo

ESP-IDF firmware that runs the Agora MyBot Device API flow on Mosaico ESP32-S31.
The application issues a pairing code, waits for Bot Station binding, starts the
assigned conversation, and joins the returned RTC channel through duplex WHIP.
Mosaico's ES8311 microphone is the mono uplink source. The ES8311 speaker plays
the downlink. ESP-SR full-duplex software AEC runs at 16 kHz using the codec's
DAC reference captured synchronously with the microphone.

This repository is a source-only snapshot of development commit
`f97c0421f3178a4a6a39b0bf6787c6123e7dd60b`. It retains the v3 touchscreen layout
and current application behavior. See [SOURCE.md](SOURCE.md) for provenance
and [VERSIONING.md](VERSIONING.md) for the public repository's version scheme.

## Download the build environment

ESP-IDF, the compiler, Python environment, third-party dependencies and build
outputs are **not included in this repository**. They are downloaded into this
project's own ignored directories. No neighboring project or global ESP-IDF
installation needs to be changed or activated.

The documented host is macOS. Install Python 3.10 or newer, Git, CMake, Ninja
and Xcode Command Line Tools. The host tests need Clang with ASan/UBSan support.
With Homebrew, install the additional tools using:

```sh
brew install python cmake ninja
```

If Xcode Command Line Tools are not already installed:

```sh
xcode-select --install
```

Clone and download the pinned environment:

```sh
git clone https://github.com/qiuyanli1990/mosaico-BotStation.git
cd mosaico-BotStation
python3 scripts/bootstrap.py --install-idf
```

Network access to GitHub, Espressif's download servers, the ESP Component
Registry and Python package indexes is required. Allow several gigabytes of
free disk space for the SDK, toolchain, downloaded components and build.

| Dependency | Exact revision |
| --- | --- |
| ESP-IDF | `7b9cc1ac79f865983f59bb8ff3ff43eb74ff1dbe` |
| Mosaico BSP | `a7a93d290be28d18fbfd32361c90aa20b0cd1c67` |
| Agora WebRTC reference | `b57d626c9a885c75a216a5b2deccc6bc2fe2e045` |

The bootstrap applies the two existing integration patches: the compatible
`esp_codec_dev` requirement and the ES8311 DAC-reference configuration used by
software AEC. See [THIRD_PARTY.md](THIRD_PARTY.md) for their details and licenses.
The wrapper checks the pinned ESP-IDF commit and uses version `6.2.0`. This is
an experimental ESP32-S31 target; an arbitrary stable or latest SDK is not a
substitute for the pinned revision.

Registry component versions and hashes are retained in `dependencies.lock`.
The wrapper loads `scripts/component-constraints.txt` to prevent automatic
upgrades when conditional display dependencies are resolved on first setup.
Keep both files when cloning or exporting this application.

| Local directory | Created by | Contents |
| --- | --- | --- |
| `.tools/esp-idf/` | bootstrap | Pinned SDK checkout |
| `.tools/espressif/` | bootstrap | Compiler, ROM files and Python environment |
| `third_party/` | bootstrap | Pinned BSP and WebRTC source with integration patches |
| `managed_components/` | ESP-IDF component manager | Registry components |
| `build/` | firmware build | Configuration, object files, firmware and logs |
| `build-ui-host/` | UI tests | Host binaries and generated previews |

These directories are ignored by Git. Bootstrap again after a new clone;
Python environments and build directories contain paths to their own clone.

## Configure and build

The Agora App ID and its matching App Certificate sign the local WHIP JWT.
The RTC token returned by Bot Station is not used for that authentication.
Create your own local settings from the placeholder-only template:

```sh
cp sdkconfig.local.example sdkconfig.local
```

Edit `sdkconfig.local` with your own App ID and matching App Certificate:

```text
CONFIG_MYBOT_APP_ID="YOUR_APP_ID"
CONFIG_MYBOT_APP_CERT="YOUR_APP_CERTIFICATE"
```

The default Device API is `https://botstation.agora.io/api`; the default hardware
model is `mosaico`. Access to Bot Station and its cloud conversation services is
required. Wi-Fi credentials are optional; configure them on the touchscreen
instead. `sdkconfig.local` accepts only `CONFIG_MYBOT_*` settings and is reapplied
on subsequent builds. Keep it, generated `sdkconfig`, and firmware images
containing credentials out of Git.

Configure the preview target and build through the project wrapper:

```sh
bash scripts/idf.sh --preview set-target esp32s31
bash scripts/idf.sh build
```

The first configuration downloads the managed components. The resulting
application is `build/mosaico_mybot.bin`, with its ELF, bootloader and partition
table alongside it. Building does not flash a device or start a cloud session.

This is a standalone application with its own partition table. Choose and
validate an installation/recovery process for your board before replacing an
existing factory Mosaico installation.

For a board already installed with this application's partition layout, enter
download mode and select the actual download-mode serial port for an
application-only update:

```sh
bash scripts/idf.sh -p /dev/cu.usbmodemDOWNLOAD app-flash
```

Find the newly enumerated runtime CDC port after boot:

```sh
ls /dev/cu.*
bash scripts/idf.sh -p /dev/cu.usbmodemRUNTIME monitor --no-reset
```

Replace the example port names with your actual ports. Exit the monitor with
Ctrl+]. Use a data-capable cable on the board's USB connector.

## Run the tests

After configuration has downloaded the managed components:

```sh
bash tests/host/run_aec_tests.sh
bash tests/ui/run_ui_tests.sh
```

The AEC suite runs the production capture/routing code under ASan/UBSan with
mocked hardware and DSP services. The UI suite runs the actual LVGL renderer,
production UI callbacks and power service with fixtures. It covers navigation,
Wi-Fi, pairing, conversation controls, physical-button fallback, brightness
and battery handling; previews go to `build-ui-host/previews/`.
Acoustic AEC quality, real battery behavior and live Bot Station connectivity
still require board and account validation.

## Pair and run

1. Boot the device and connect it to Wi-Fi using the touchscreen if needed.
   Wi-Fi setup closes automatically after the device gets an IP address. If a
   saved Wi-Fi connection fails, the setup screen opens so you can select a network.
2. For an unpaired device, the screen displays the large, centered six-digit
   MyBot pairing code. Claim the device in Bot Station. A saved binding is verified
   and reused without requesting another code.
3. Once binding is confirmed, the app automatically starts the Bot Station
   conversation, signs a short-lived WHIP JWT, and joins the assigned RTC channel.
   A centered bulb indicates the conversation state: connecting, listening,
   thinking, or speaking. During automatic startup the control shows
   **Connecting...** instead of offering to start again.
4. A compact **Stop** button above the volume slider closes RTC and stops the
   Bot Station session. It stays stopped until you press **Start** again. If
   startup fails, press **Retry**. Each boot automatically starts once after
   binding is confirmed; the button is hidden during pairing.

The v3 home dashboard shows the time, seconds and date in Beijing time (UTC+8),
plus Wi-Fi status and the network name. Tap the Wi-Fi badge to open setup.
The dashboard and setup pages share a playful cream/peach theme with little
clouds, polka dots and rounded pastel controls. The smiling bulb stays at the
center of the 480 x 480 display: green for listening, yellow for thinking,
orange for speaking, red for conversation errors, teal while connecting and a
muted neutral while stopped. Small sparkles move during connection, thinking
and speaking. The session timer measures connected time.

Short-click the top orange **AI** button to start or stop a conversation. It uses
the BSP's `BSP_BUTTON_AI` (GPIO 7) and the same control path as the touchscreen.
Repeated commands are ignored while starting or stopping; before pairing it
does not start a session, and without Wi-Fi it opens setup. The compact screen
control remains above the volume slider.

Tap the bulb or **Chat** to view the latest user and assistant messages when the
agent supplies transcripts. This is a latest-message view, not a chat archive.
The bottom slider controls speaker volume from 0 to 100 percent, including during
conversations. Tap its speaker icon to mute and restore the last nonzero volume.
The brightness button uses a sun icon and opens a sheet with a 10-100 percent
slider plus Dim, Balanced and Bright presets. The last brightness is saved in
NVS after adjustments settle and restored at startup. The header displays the
BQ27220's battery percentage, low-charge color and charging/full indication;
missing or unreadable battery data has an explicit placeholder.
See [the v3 screen design and rendered previews](docs/V3_UI.md), or the
[v2 design baseline](docs/V2_UI.md).

Clock synchronization runs in the background as soon as Wi-Fi gets an IP,
using `ntp.aliyun.com` with `pool.ntp.org` as a fallback and no random startup
delay. A cold boot still waits for valid time before HTTPS and WHIP authentication;
the screen uses the normal connecting prompt during this wait. Once the clock is
valid, later conversations proceed without waiting for another synchronization.
The dashboard displays a placeholder until the clock is valid; displaying local
time does not change the UTC clock used by authentication.

Only the long-lived MyBot device credential is stored locally. The pairing
code and temporary pair token stay in memory.

Conversation startup logs `agent_id` and `conversation_id` under `BOT_STATION`,
then the RTC channel, local UID and agent UID under `MYBOT_SESSION`. During an
active session, `MYBOT_SESSION` repeats both IDs and the Wi-Fi/RTC connection
state every ten seconds, so a monitor attached after startup can identify the
current conversation. Stop logs include the conversation ID and stop reason.
The API's bound `agent_id` and a cloud ConvoAI task ID are different identifiers;
keep `conversation_id` and channel alongside the bound agent ID when correlating
with cloud logs. WHIP bearer credentials are not printed.

## Mosaico audio and memory

PSRAM, display and ES8311 initialization use the pinned Mosaico BSP.
Large audio rings and
long-lived PCM buffers use external RAM; DMA buffers remain allocated by the
Mosaico BSP and peripheral drivers. The MyBot WebRTC provider requests 48 kHz,
16-bit mono Opus audio in both directions. The Device API conversation request
declares Opus with a 20 ms packet interval, matching the RTC provider.

To pair the device again, clear the `device_token` value in the `bot_station`
NVS namespace before booting it. Wi-Fi settings are stored separately.

Hardware acceptance still needs to check Wi-Fi provisioning, pairing and binding,
WHIP connectivity, microphone capture, AEC performance, and speaker playback on
the target Mosaico revision.

## Echo cancellation

The ES8311 supplies a digital DAC reference; ESP-SR performs software echo
cancellation. The board documentation lists an ES8311 codec and NS4150B speaker
amplifier, without a dedicated AEC processor. The implementation follows the
[ESP32-S31 ESP-SR AEC guide](https://docs.espressif.com/projects/esp-sr/zh_CN/latest/esp32s31/acoustic_echo_cancellation/README.html)
and the installed `esp_codec_dev` ES8311 driver's `no_dac_ref` contract.
See the official [Mosaico V1.2 board guide](https://docs.espressif.com/projects/esp-dev-kits/zh_CN/latest/esp32s31/esp-mosaico/user_guide.html).

The capture path is:

```text
ES8311 REG44=0x58, no_dac_ref=false
  -> 48 kHz / 16-bit stereo [ADC microphone, DAC reference], shared sample clock
  -> esp_ae_rate_cvt: both channels together, 48 -> 16 kHz
  -> ESP-SR AEC_MODE_FD_HIGH_PERF, one mic + one ref, NLP=AGGR
  -> esp_ae_rate_cvt: clean mono, 16 -> 48 kHz
  -> 48 kHz mono Opus uplink, 20 ms packets
```

Capture uses blocking I2S reads without a second software pacing clock. It
collects complete AEC chunks and returns real processed audio without padding
short reads with silence. The filter, both resamplers, and queued samples reset
between conversations. Missing reference configuration and processing failures
stop capture with an explicit error. The previous independent software playback
reference FIFO and sample-dropping/repeating rate conversion have been removed.

Every five seconds, `AEC stats` logs microphone/reference/output RMS, active
reference frames, microphone clipping, processing time and queued duration.
During speaker playback, `ref_rms` and `ref_active` must rise. These counters
diagnose routing and load; the RMS values alone do not establish echo reduction
or measure ERLE, particularly during double talk.

For an acoustic A/B comparison, enable `CONFIG_MYBOT_AEC_BYPASS=y` in menuconfig
and build a diagnostic firmware. This preserves mic/ref capture and resampling
while bypassing cancellation, and prints an explicit bypass warning at boot.
Disable it for normal firmware; its default is off.

Real-device acceptance should compare normal and bypass firmware at the same
distance, room conditions and volume:

| Scenario | Check |
| --- | --- |
| Bot speaks, user silent | DAC reference is present; the uplink recording has substantially less bot playback and does not trigger self-replies. |
| User speaks, bot silent | Speech remains clear, continuous and intelligible. |
| Both speak | The user can interrupt; AEC must preserve the user's speech during playback. |
| Low, medium and high speaker volume | Check residual echo and `mic_clip`; repeat after Stop/Start to verify filter reset. |

Host regression tests run the actual capture and AEC routing code with mocked
codec, AEC and resampling APIs:

```sh
bash tests/host/run_aec_tests.sh
```

They check paired channel order, arbitrary input splits and output frame sizes,
continuity, reset and initialization failures, missing reference configuration,
processing failures and queue overflow under ASan/UBSan. They exercise normal
and bypass builds at AEC chunk sizes 160, 256 and 512. They verify software
routing and memory safety, while acoustic performance requires the board tests.
