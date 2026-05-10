#!/home/me/tools/asr-kb/asr-env/bin/python3
import keyboard
import pyautogui
import sounddevice as sd
import numpy as np
import requests
import time
import threading

SERVER_URL = "http://127.0.0.1:8000/transcribe"
HOTKEY = "ctrl+shift+k"
HOTKEY2 = "ctrl+shift+a"

recording_lock = threading.Lock()
recording_active = False


def record_while_key_held():
    start_time = time.time()
    
    try:
        device_info = sd.query_devices(8)
        native_rate = int(device_info['default_samplerate'])
    except Exception as e:
        print(f"Error accessing audio device 8: {e}")
        return None, None

    collected = []

    def callback(indata, frames, time_info, status):
        if status:
            print(f"Status: {status}")
        collected.append(indata.copy())

    stream = sd.InputStream(device=8, channels=1, samplerate=native_rate, callback=callback)
    stream.start()
    print("Recording...")

    # Wait here while the hotkey is being held down
    while recording_active:
        time.sleep(0.01)

    stream.stop()
    stream.close()
    print("Stopped recording")

    if not collected:
        return None, None

    audio_data = np.concatenate(collected).flatten()
    elapsed = time.time() - start_time
    print(f"Duration: {elapsed:.1f}s")

    return audio_data.tobytes(), native_rate


def transcribe_and_type(add_enter=False):
    audio_bytes, sample_rate = record_while_key_held()
    
    if not audio_bytes:
        return

    hex_bytes = audio_bytes.hex()
    start = time.time()
    
    try:
        response = requests.post(SERVER_URL, json={
            "audio_bytes": hex_bytes,
            "sample_rate": sample_rate
        }, timeout=30)
        result = response.json()
        transcript = result.get("transcript", "")
    except Exception as e:
        print(f"Server error: {e}")
        return

    elapsed = time.time() - start
    print(f"Latency: {elapsed:.3f}s")

    if transcript and transcript != "[No speech detected]":
        pyautogui.write(transcript)
        if add_enter:
            pyautogui.press("enter")
        print(f"Typed: {transcript}")


def on_key_down(add_enter):
    global recording_active
    with recording_lock:
        # Check if we are already recording to ignore OS key-repeat spam
        if not recording_active:
            recording_active = True
            # Spawn a thread so we don't block the keyboard listener
            threading.Thread(target=transcribe_and_type, args=(add_enter,), daemon=True).start()


def on_key_up():
    global recording_active
    with recording_lock:
        # Releasing the key breaks the while loop inside record_while_key_held()
        if recording_active:
            recording_active = False


if __name__ == "__main__":
    print(f"Listening for hotkeys: {HOTKEY}, {HOTKEY2}")
    print(f"Hold {HOTKEY} to record, release to transcribe.")
    print(f"Hold {HOTKEY2} to record, release to transcribe + Enter.")

    # Hotkey 1: Normal typing (add_enter = False)
    keyboard.add_hotkey(HOTKEY, on_key_down, args=(False,), suppress=True)
    keyboard.add_hotkey(HOTKEY, on_key_up, suppress=True, trigger_on_release=True)
    
    # Hotkey 2: Type and press Enter (add_enter = True)
    keyboard.add_hotkey(HOTKEY2, on_key_down, args=(True,), suppress=True)
    keyboard.add_hotkey(HOTKEY2, on_key_up, suppress=True, trigger_on_release=True)

    keyboard.wait()