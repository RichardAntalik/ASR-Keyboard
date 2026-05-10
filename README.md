# asr-kb

Real-time speech-to-text tool that records audio while holding a hotkey and transcribes it into typed text.

## Components

- **asr-kb.py** — Client: listens for `Ctrl+Shift+K`, records audio while held, sends to server, types the transcript
- **asr-server.py** — Server: FastAPI endpoint that transcribes audio using `ibm-granite/granite-speech-4.1-2b`

## Setup

```bash
bash install.sh
```

## Usage

1. Start the server: `sudo ./asr-server.py`
2. Start the client: `sudo ./asr-kb.py`
3. Hold `Ctrl+Shift+K` to record, release to transcribe

## Dependencies

See `requirements.txt`.
