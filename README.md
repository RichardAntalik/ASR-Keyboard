# asr-kb

Real-time speech-to-text tool that records audio while holding a hotkey and transcribes it into typed text.

## Components

- **asr-kb.py** — Client: listens for `Ctrl+Shift+K`, records audio while held, sends to server, types the transcript
- **asr-server.py** — Server: FastAPI endpoint that transcribes audio using `ibm-granite/granite-speech-4.1-2b`
- **asr-kb.c** — C client alternative: uses X11/XInput2 for hotkeys, PulseAudio for recording, XTest for typing

## Architecture

```
[Microphone] → [Client (hold hotkey)] → [HTTP POST] → [Server (Granite model)] → [Keyboard simulation]
```

## Setup

```bash
bash install.sh
```

This creates a Python virtual environment (`asr-env`) and installs all dependencies from `requirements.txt`.

## Usage

### Python client

1. Start the server: `sudo ./asr-server.py`
2. Start the client: `sudo ./asr-kb.py --device <number>` (default: 8)
3. Hold `Ctrl+Shift+K` to record, release to transcribe
4. Hold `Ctrl+Alt+K` to record, release to transcribe + Enter

### C client

1. Build: `sudo ./install.sh`
2. Run: `sudo ./asr-kb`
3. Hold configured shortcut to record, release to transcribe
4. Config file: `~/.config/asr-kb/config.json` (see below)

Compile the C client with: `cmake . && make && sudo make install`

## Dependencies

See `requirements.txt`.

## Audio Device Selection

To find your microphone device number:

```python
import sounddevice as sd
sd.query_devices(kind='input')
```

Pass `--device <number>` when running `asr-kb.py` and `ASRClient.py`.

## Config File

Create `~/.config/asr-kb/config.json`:

```json
[
  { "shortcut": ["ctrl", "super", "space"], "prompt": "default", "special_key": "null" },
  { "shortcut": ["ctrl", "super", "alt", "space"], "prompt": "default", "special_key": "enter" }
]
```

- `shortcut`: array of key names (ctrl, super, alt, space, etc.)
- `prompt`: prompt to send to server (currently "default" only)
- `special_key`: optional special key after transcribe (enter, null)

Default config is used if file not found.

## Hotkey Customization

Edit `HOTKEY` and `HOTKEY2` variables in `asr-kb.py` to change the hotkeys.

## Hardware Requirements

- **CPU**: The model runs on CPU but is slow. Expect high latency.
- **CUDA GPU**: Recommended. The model uses bfloat16 on CUDA for faster inference.
- **RAM**: The model (~2B parameters) requires significant memory. Ensure your system has enough.

## Model Download

The first run downloads `ibm-granite/granite-speech-4.1-2b` from Hugging Face. This is a large download.

## Troubleshooting

- **No audio**: Check that your microphone device number is correct.
- **Server not responding**: Ensure `asr-server.py` is running on localhost:8000.
- **Typing not working**: Ensure `pyautogui` has permission to simulate keyboard input.
- **Slow transcription**: Use `--device cuda` on the server if you have a GPU.

## License

MIT. See `LICENSE` for details.
