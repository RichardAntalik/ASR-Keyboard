#!/usr/bin/env python3
from typing import Union
from fastapi import FastAPI
from fastapi.responses import JSONResponse, PlainTextResponse
from pydantic import BaseModel
import torch
import numpy as np
from transformers import AutoProcessor, AutoModelForSpeechSeq2Seq
import torchaudio
import argparse
import base64

parser = argparse.ArgumentParser(description="ASR server")
parser.add_argument("--device", choices=["cpu", "cuda"], default="cpu", help="Compute device")
parser.add_argument("--debug", action="store_true", help="Enable debug timing")
args = parser.parse_args()

app = FastAPI()

model_id = "ibm-granite/granite-speech-4.1-2b"
device = args.device
MODEL_SAMPLE_RATE = 16000

print(f"Loading {model_id} on {device}...")
try:
    processor = AutoProcessor.from_pretrained(model_id, local_files_only=True)
except (ValueError, OSError):
    processor = AutoProcessor.from_pretrained(model_id)

tokenizer = processor.tokenizer

try:
    model = AutoModelForSpeechSeq2Seq.from_pretrained(
        model_id,
        dtype=torch.bfloat16 if device == "cuda" else torch.float32,
        low_cpu_mem_usage=True,
        local_files_only=True
    ).to(device)
except (ValueError, OSError):
    model = AutoModelForSpeechSeq2Seq.from_pretrained(
        model_id,
        dtype=torch.bfloat16 if device == "cuda" else torch.float32,
        low_cpu_mem_usage=True
    ).to(device)
print("Model loaded.")


class TranscribeRequest(BaseModel):
    audio_bytes: str
    sample_rate: int
    prompt: str
    request_id: int = 0
    target_window: int = 0


class TranscribeCRequest(BaseModel):
    audio: str
    rate: int


@app.post("/transcribe", response_model=None)
def transcribe(req: TranscribeRequest) -> Union[JSONResponse, PlainTextResponse]:
    import time
    start_time = time.time()

    # Change fromhex to b64decode
    try:
        audio_raw = base64.b64decode(req.audio_bytes)
        # Use the correct dtype! 
        # If your client sends raw PCM 16-bit, use np.int16. 
        # If it sends raw floats, use np.float32.
        audio_data: np.ndarray = np.frombuffer(audio_raw, dtype=np.int16)
    except Exception as e:
        return JSONResponse(content={"transcript": ""}, status_code=400)

    audio_tensor = torch.from_numpy(np.array(audio_data)).unsqueeze(0)

    if audio_tensor.shape[-1] < 512:
        return JSONResponse(content={"transcript": ""})

    audio_tensor = audio_tensor / audio_tensor.abs().max()

    if req.sample_rate != MODEL_SAMPLE_RATE:
        resampler = torchaudio.transforms.Resample(
            orig_freq=req.sample_rate, new_freq=MODEL_SAMPLE_RATE
        )
        audio_tensor = resampler(audio_tensor)

    chat = [{"role": "user", "content": req.prompt}]
    prompt = tokenizer.apply_chat_template(chat, tokenize=False, add_generation_prompt=True)

    inputs = processor(prompt, audio_tensor, sampling_rate=MODEL_SAMPLE_RATE, return_tensors="pt").to(device)
    input_token_len = inputs["input_ids"].shape[-1]

    with torch.no_grad():
        output_ids = model.generate(**inputs, max_new_tokens=1024, do_sample=False, num_beams=1)

    generated_ids = output_ids[:, input_token_len:]
    transcript = tokenizer.batch_decode(generated_ids, add_special_tokens=False, skip_special_tokens=True)[0]

    if args.debug:
        duration = time.time() - start_time
        print(f"Transcription took {duration:.4f} seconds")

    if not transcript.strip():
        return JSONResponse(content={"transcript": ""})
    return JSONResponse(content={"transcript": transcript.strip()})


if __name__ == "__main__":
    import uvicorn
    uvicorn.run(app, host="127.0.0.1", port=8000)
