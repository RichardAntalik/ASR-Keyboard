#!/home/me/tools/asr-kb/asr-env/bin/python
from typing import Optional, Tuple
import sounddevice as sd
import numpy as np
import scipy.io.wavfile as wav
import requests
import io

MODEL_SAMPLE_RATE = 16000
SERVER_URL = "http://127.0.0.1:8000/transcribe"


def record_audio(duration: float = 5.0, device: Optional[int] = None) -> Tuple[bytes, int]:
    """Record audio and return raw float32 bytes and sample rate."""
    print("Recording...")
    if device is None:
        device_info = sd.query_devices(kind='input')
        native_rate = int(device_info['default_samplerate'])
    else:
        device_info = sd.query_devices(device)
        native_rate = int(device_info['default_samplerate'])

    samples = int(duration * native_rate)
    audio_data = sd.rec(samples, samplerate=native_rate, channels=1, dtype='float32', device=device)
    sd.wait()

    audio_data = audio_data.flatten()
    print("Done!")

    return audio_data.tobytes(), native_rate


def transcribe_audio(duration: float = 5.0, device: Optional[int] = None) -> str:
    """Record audio and send to ASR server for transcription."""
    audio_bytes, sample_rate = record_audio(duration, device=device)

    # Convert to hex for transport
    hex_bytes = audio_bytes.hex()

    response = requests.post(SERVER_URL, json={
        "audio_bytes": hex_bytes,
        "sample_rate": sample_rate
    })

    result = response.json()
    transcript = result["transcript"]

    print(f"RESULT: {transcript}")
    return transcript


if __name__ == "__main__":
    try:
        import time
        start = time.time()
        transcript = transcribe_audio(device=8)
        elapsed = time.time() - start
        print(f"Latency: {elapsed:.3f}s")
    except KeyboardInterrupt:
        print("\nExiting...")
