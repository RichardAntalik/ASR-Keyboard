#!/home/me/tools/asr-kb/asr-env/bin/python3
import keyboard
import pyautogui
import sounddevice as sd
import numpy as np
import requests
import time

SERVER_URL = "http://127.0.0.1:8000/transcribe"
MODEL_SAMPLE_RATE = 16000
HOTKEY = "ctrl+shift+k"
MAX_RECORD_DURATION = 10.0


def record_while_key_held():
    start_time = time.time()
    device_info = sd.query_devices(8)
    native_rate = int(device_info['default_samplerate'])

    total_samples = int(MAX_RECORD_DURATION * native_rate)
    audio_data = sd.rec(total_samples, samplerate=native_rate, channels=1, dtype='float32', device=8)
    print("Recording...")

    while keyboard.is_pressed(HOTKEY):
        time.sleep(0.01)

    print("Stopped recording")

    sd.wait()
    audio_data = audio_data.flatten()

    elapsed = time.time() - start_time
    actual_samples = int(elapsed * native_rate)
    audio_data = audio_data[:actual_samples]

    return audio_data.tobytes(), native_rate


def transcribe_and_type():
    audio_bytes, sample_rate = record_while_key_held()
    hex_bytes = audio_bytes.hex()

    start = time.time()
    response = requests.post(SERVER_URL, json={
        "audio_bytes": hex_bytes,
        "sample_rate": sample_rate
    })
    result = response.json()
    transcript = result["transcript"]

    elapsed = time.time() - start
    print(f"Latency: {elapsed:.3f}s")

    if transcript != "[No speech detected]":
        pyautogui.write(transcript)
        print(f"Typed: {transcript}")


if __name__ == "__main__":
    print(f"Listening for hotkey: {HOTKEY}")
    print("Press and hold Ctrl+Shift+K to record, release to transcribe.")

    keyboard.add_hotkey(HOTKEY, transcribe_and_type)

    try:
        keyboard.wait()
    except KeyboardInterrupt:
        print("\nExiting...")
