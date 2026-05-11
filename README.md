# asr-kb

Install dependencies, build with `cmake . && make`, then run `./asr-kb`. Start the server at `localhost:8000` beforehand.

Hold a configured hotkey combination to record audio from your microphone, release it to transcribe the speech, and the tool types the transcript directly into your active window.

Two hotkeys are supported: one for plain typing, one for typing plus an Enter key.

## Configuration

Create `~/.config/asr-kb/config.json`:

```json
[
  { "shortcut": ["ctrl", "super", "space"], "prompt": "<|audio|>transcribe the speech with proper punctuation and capitalization.", "special_key": "null" },
  { "shortcut": ["ctrl", "super", "alt", "space"], "prompt": "<|audio|>transcribe the speech with proper punctuation and capitalization.", "special_key": "enter" }
]
```

- `shortcut`: array of key names (ctrl, super, alt, space, etc.)
- `prompt`: prompt sent to the server for transcription
- `special_key`: optional key pressed after transcribing (enter, tab, null)

Entries are sorted by key count (descending) to prevent subset overlap.

## Example

Voice input: "run the docker build command"
Output (typed): `docker build`

Voice input: "what is the current date"
Output (typed): `What is the current date.` + Enter

## Usage

```bash
sudo ./asr-kb                    # run with default config
sudo ./asr-kb -l                 # list audio sources
sudo ./asr-kb -i 3               # select audio source by index
sudo ./asr-kb -d                 # enable debug output
sudo ./asr-kb -c /path/config.json  # specify config file
sudo ./asr-kb -h                 # show help
```

## Architecture

```
[Microphone] → [Client (hold hotkey)] → [HTTP POST] → [Server (Granite model)] → [Keyboard simulation]
```

## Components

- **src/main.cpp** — Argument parsing, config loading, XInput event loop, server communication
- **src/pulse-recording.cpp** — PulseAudio source listing and audio recording
- **src/keyboard-sim.cpp** — XTest keyboard simulation for typing the transcript

## Build

```bash
cmake . && make
sudo make install
```

Binary `asr-kb` is placed in the project directory.

## Dependencies

X11, PulseAudio, libcurl, pthreads, nlohmann/json.

## Troubleshooting

- **No audio**: Use `./asr-kb -l` to list sources, then `./asr-kb -i <index>` to select the correct microphone.
- **Server not responding**: Ensure `asr-server.py` is running on localhost:8000.
- **Typing not working**: Run with sudo for keyboard input permissions.
- **Slow transcription**: Use `--device cuda` on the server if you have a GPU.

## License

MIT. See `LICENSE` for details.
