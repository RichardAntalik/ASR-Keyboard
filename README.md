# asr-kb

Hold a configured hotkey combination to record audio from your microphone, release it to transcribe the speech, and the tool types the transcript directly into your active window.

## Installation

### 1. System Dependencies
Ensure you have X11, PulseAudio, libcurl, and a C++ compiler installed.

### 2. Server Setup
Install Python dependencies and start the server:
```bash
pip install -r requirements.txt
python asr-server.py
```

### 3. Client Build
Build the client binary:
```bash
cmake . && make
```

## Configuration

The program will prompt you to generate a default configuration if `~/.config/asr-kb/config.json` is missing or invalid.

### Default Configuration
```json
[
  { "shortcut": ["ctrl", "super", "space"], "prompt": "transcribe the speech with proper punctuation and capitalization.", "special_key": "null" },
  { "shortcut": ["ctrl", "super", "alt", "space"], "prompt": "transcribe the speech with proper punctuation and capitalization.", "special_key": "enter" }
]
```

- `shortcut`: array of key names (ctrl, super, alt, space, etc.)
- `prompt`: prompt sent to the server for transcription
- `special_key`: optional key pressed after transcribing (enter, tab, null)

Entries are sorted by key count (descending) to prevent subset overlap.

## Example

(todo)

## Usage

```bash
./asr-kb                    # run with default config
./asr-kb -l                 # list audio sources
./asr-kb -i 3               # select audio source by index
./asr-kb -d                 # enable debug output
./asr-kb -c /path/config.json  # specify config file
./asr-kb -a                 # remember active window for text injection
./asr-kb -h                 # show help
```

## Canceling with Escape

Press **Escape** at any point to cancel:

- **During recording** — stops recording, discards captured audio
- **While waiting for server** — aborts the HTTP request, discards the transcript
- **While typing** — stops keyboard simulation mid-character

Escape can be pressed at any time, even while the server is processing transcription.

## Architecture

```
[Microphone] → [Client (hold hotkey)] → [HTTP POST] → [Server (Granite model)] → [Keyboard simulation]
```

The main thread runs the X11 event loop (always responsive to key events). Recording runs in a worker thread. After recording ends, the HTTP request runs in a separate thread so the event loop remains responsive — allowing Escape to cancel at any point.

## Components

- **src/main.cpp** — Application entry, config prompts, XInput event loop, and thread coordination
- **src/config-parsing.cpp** — Configuration loading, validation, and generation
- **src/client.cpp** — Communication with the ASR server via HTTP (with timeout)
- **src/pulse-recording.cpp** — PulseAudio source listing and audio recording
- **src/keyboard-sim.cpp** — XTest keyboard simulation for typing transcripts

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

X11, PulseAudio, libcurl, pthreads, nlohmann/json.

## Troubleshooting

- **No audio**: Use `./asr-kb -l` to list sources, then `./asr-kb -i <index>` to select the correct microphone.
- **Server not responding**: Ensure `asr-server.py` is running.
- **Typing not working**: Check if your X11 session allows keyboard simulation.
- **Slow transcription**: Use `--device cuda` on the server if you have a GPU.

## License

MIT. See `LICENSE` for details.

