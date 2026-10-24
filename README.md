# Xiaozhi Knowledge Radio

Add internet radio stations to a **xiaozhi.me knowledge base** and ask your ESP32 voice assistant to play them by name.

This repository contains a radio playback module and an integration patch for [78/xiaozhi-esp32](https://github.com/78/xiaozhi-esp32). It is a firmware extension, not a standalone application or an external MCP server.

## How it works

```text
“Play my radio station”
          ↓
Xiaozhi agent searches the Radios knowledge base
          ↓
Finds the matching active station and its exact URL
          ↓
self.radio.play(name, url)
          ↓
ESP32 connects to the stream and plays audio
```

**The cloud agent queries the knowledge base.** The ESP32 receives the selected URL through MCP; it does not download the spreadsheet or call a private knowledge-base API. Once the firmware is integrated, adding stations only requires updating the knowledge-base document.

## Add your stations

1. Copy [examples/radios.csv](examples/radios.csv) and replace its placeholder with your station details.
2. In the Xiaozhi console, create a knowledge base named **Radios** and upload the UTF-8 CSV.
3. In your agent's settings, enable Knowledge Base, select **Radios**, and save.
4. Append [the radio instructions](docs/agent-instructions.md) to your existing agent instructions and save.
5. Connect a device running the integrated firmware and confirm that `self.radio.play`, `self.radio.stop`, and `self.radio.status` are available.
6. Ask `Play <station name or alias>`. To stop, ask “Stop the radio”.

The CSV retains the original field names for compatibility:

| Field | Meaning | Example |
|---|---|---|
| `nome` | Station name | Example Radio |
| `apelido` | Spoken alias | my radio |
| `url` | Complete direct stream URL | `https://radio.example.com/stream` |
| `ativa` | Whether playback is allowed | `true` or `false` |

The example uses a reserved domain and is inactive. Replace it with a real stream and set `ativa` to `true`. Use direct HTTP(S) AAC/ADTS, AAC+, or MP3 streams. Website pages, M3U/PLS playlists, and HLS streams are not supported.

To add stations, update the CSV and replace the previous document in the knowledge base to avoid duplicate records. Wait for indexing and test retrieval by name and alias. Semantic search may return an unrelated candidate: the agent instructions require a matching station and prohibit invented URLs.

## Firmware integration

Pinned upstream commit: `7e69845d818553ac045efbcde746b22bef6eecf6`.

The patch registers and enables the radio tools for **Spotpear ESP32-S3 1.28 BOX**. Other boards require their own integration and validation. Do not flash a build intended for a different board.

Requirements: Git, Python 3, and a configured ESP-IDF release/v6.1 environment (upstream minimum: 6.0.1). Run these commands from this repository's root, using a shell with `git`, `cp`, and Python:

```sh
git clone https://github.com/78/xiaozhi-esp32.git xiaozhi-esp32
git -C xiaozhi-esp32 checkout 7e69845d818553ac045efbcde746b22bef6eecf6
git -C xiaozhi-esp32 apply --check ../integration/xiaozhi-radio.patch
git -C xiaozhi-esp32 apply ../integration/xiaozhi-radio.patch
cp -R main/radio xiaozhi-esp32/main/
cd xiaozhi-esp32
python scripts/build.py spotpear/sp-esp32-s3-1.28-box --name sp-esp32-s3-1.28-box --language en-US
```

In PowerShell, replace the `cp` command with `Copy-Item -Recurse main/radio xiaozhi-esp32/main/`. On Windows, use paths without accented characters for the SDK and build checkout. Follow upstream instructions for dependencies, assets, and flashing your board.

The patch integrates radio playback with the application's audio lifecycle and enables `CONFIG_ENABLE_RADIO_PLAYER=y`. It does not change GIFs, subtitles, themes, board pins, or flash mode. Original playback error messages are currently in Portuguese; MCP tool names and descriptions are in English.

## Device tools

| Tool | Arguments | Result |
|---|---|---|
| `self.radio.play` | `name`, `url` | Accepts and queues a validated request |
| `self.radio.stop` | None | Cancels pending connection or playback |
| `self.radio.status` | None | Reports state, station name, and any error |

`accepted`, `queued`, and `connecting` do not confirm playback. `playing` means PCM has reached the audio queue, not that audible output was measured. The conversation releases the audio channel for streaming. The device button or wake word interrupts the radio; request the station again to resume. Volume uses the device's normal volume tool.

## Tests and current status

```sh
g++ -std=c++20 -Wall -Wextra tests/radio/test_radio_stream.cc -o radio-tests
./radio-tests
```

Host tests cover URL validation, fragmented ICY metadata filtering, stale-request cancellation, and power-release decisions. CI also checks that the integration patch applies to the pinned upstream commit. It does not run physical-device or live-stream tests.

The originating implementation compiled with ESP-IDF 6.1 for ESP32-S3. This extraction preserves the same radio, application, and audio code, and its patch application and host tests were verified locally. Physical audio, reconnect behavior, long-running stability, and voice-recognition coexistence still require device testing. No prebuilt firmware is included. The English build-language variant shown above has not been separately compiled.

TLS certificate verification remains enabled. Stream URLs are accessed from the device's network: add only trusted stations and do not publish URLs containing credentials. Knowledge-base documents are station data, not instructions for the agent to execute.

## Repository contents

- `main/radio/`: AAC/MP3 player and device-side MCP tools.
- `integration/`: application/audio patch and pinned upstream metadata.
- `docs/agent-instructions.md`: agent configuration instructions in English.
- `examples/radios.csv`: station-list template.
- `tests/`: host tests.

## License

MIT. Upstream Xiaozhi copyright notices are retained in [LICENSE](LICENSE). This is an independent extension and is not officially affiliated with Xiaozhi or Tenclass.
