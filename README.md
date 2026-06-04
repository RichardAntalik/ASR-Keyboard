# asr-kb

Hold a configured hotkey combination to record audio from your microphone, release it to transcribe the speech, and the tool types the transcript directly into the window.

## Installation

### 1. System Dependencies
Ensure you have X11, PulseAudio, libcurl, and a C++ compiler installed.

### 2. One-Command Setup
```bash
sudo ./setup.sh
```

This script will:
- Create a Python virtual environment in `/opt/asr-kb/venv` (recommended) or install dependencies system-wide
- Build the client binary
- Install the binary to `/opt/asr-kb/bin/` and create a symlink at `/usr/local/bin/asr-kb`

### 3. Run the Client
```bash
asr-kb
```

The client automatically spawns the server as a subprocess. No need to start it separately.

## Configuration

The program will prompt you to generate a default configuration if the config file is missing or invalid. By default it looks for `~/.config/asr-kb/config.json` (or `$XDG_CONFIG_HOME/asr-kb/config.json` if `$XDG_CONFIG_HOME` is set).

### Default Configuration
```json
[
  { "shortcut": ["ctrl", "super", "space"], "prompt": "transcribe the speech with proper punctuation and capitalization.", "special_key": [] },
  { "shortcut": ["ctrl", "super", "alt", "space"], "prompt": "transcribe the speech with proper punctuation and capitalization.", "special_key": ["enter"] }
]
```

- `shortcut`: array of key names (ctrl, super, alt, space, etc.)
- `prompt`: prompt sent to the server for transcription
- `special_key`: optional array of keys pressed after transcribing (enter, tab, space, etc.)

Entries are sorted by key count (descending) to prevent subset overlap.

## Example

(todo)

## Usage

```bash
asr-kb                    # run with default config, spawns server automatically
./asr-kb                  # run from project directory (uninstalled)
./asr-kb -l               # list audio sources
./asr-kb -i 3             # select audio source by index
./asr-kb -d               # enable debug output
./asr-kb -c /path/config.json  # specify config file
./asr-kb -a               # type into the window that has focus when the server responds (default: active window)
./asr-kb --server-url http://127.0.0.1:8000/transcribe  # connect to an external server instead of spawning one
./asr-kb --custom-server /path/to/server.py  # use a custom server script
./asr-kb -h               # show help
```

The client automatically spawns the server as a subprocess by default. Use `--server-url` to connect to an externally running server, or `--custom-server` to specify a different server script.

## User Interface

The interface is split into two sections:

**Top section (full width):** Shortcut keys and microphone volume meter.

**Output section (50/50 split):**
- **Left half** — Client output (transcription results, debug info, status messages)
- **Right half** — Server output (FastAPI logs, model loading messages, etc.)

A vertical `|` separator divides the two output panels.

## Canceling with Escape

Press **Escape** at any point to cancel:

- **During recording** — stops recording, discards captured audio
- **While waiting for server** — server response is received but transcript is not typed
- **While typing** — stops keyboard simulation mid-character

Escape can be pressed at any time, even while the server is processing transcription. Each transcription request is assigned a unique ID; Escape marks all pending requests as cancelled. The client waits for all held keys to be released before starting to type, preventing shortcut key interference.

## Architecture

```
[Microphone] → [Client (hold hotkey)] → [HTTP POST] → [Server (Granite model)] → [Keyboard simulation]
```

The client spawns the server as a subprocess by default, finding `asr-server.py` in the same directory as the binary (or via `--custom-server`). A background reader thread captures server stdout for display in the right panel. The main thread runs the X11 event loop (always responsive to key events). Recording runs in a worker thread. After recording ends, the HTTP request runs in a separate thread so the event loop remains responsive — allowing Escape to cancel at any point.

Each transcription request is assigned a unique `request_id` (sent to the server and echoed back). Escape marks all pending requests as cancelled. The client checks the cancellation flag before processing the response and before typing — enabling per-character cancellation during keyboard simulation. Typing waits for all held keys to be released before starting.

## Components

- **src/main.cpp** — Application entry, config prompts, XInput event loop, server subprocess spawning, and thread coordination
- **src/config-parsing.cpp** — Configuration loading, validation, and generation
- **src/client.cpp** — Communication with the ASR server via HTTP (with timeout)
- **src/pulse-recording.cpp** — PulseAudio source listing and audio recording with RMS volume calculation
- **src/keyboard-sim.cpp** — XTest keyboard simulation for typing transcripts
- **src/queue.h / src/queue.cpp** — Thread-safe queue for passing recordings to the worker
- **src/request-storage.h / src/request-storage.cpp** — Per-request cancellation tracking with unique IDs
- **src/screen-manager.cpp / src/screen-manager.h** — ncurses-based UI overlay (shortcuts, VU meter, split-screen output)
- **src/vu-thread.cpp / src/vu-thread.h** — Background thread for VU meter updates

## Build

```bash
cmake . && make
```

To build with AddressSanitizer for debugging:
```bash
make debug
```

Binary `asr-kb` is placed in the project directory.

## Dependencies

X11, PulseAudio, libcurl, ncurses, pthreads, nlohmann/json.

## Troubleshooting

- **No audio**: Use `./asr-kb -l` to list sources, then `./asr-kb -i <index>` to select the correct microphone.
- **Server not found**: Install with `sudo make install` so the client can find `asr-server.py` alongside the binary.
- **Server not responding**: Use `--server-url` to connect to an externally running server.
- **Typing not working**: Check if your X11 session allows keyboard simulation.
- **Slow transcription**: Use `--device cuda` on the server if you have a GPU.

## License

MIT. See `LICENSE` for details.

