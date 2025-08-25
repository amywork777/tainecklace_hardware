#!/usr/bin/env python3
import argparse, json, os, sys, time
import torch, whisper

def to_srt(segments):
    def ts(t):
        h = int(t//3600); m = int((t%3600)//60); s = int(t%60); ms = int((t-int(t))*1000)
        return f"{h:02d}:{m:02d}:{s:02d},{ms:03d}"
    out = []
    for i, seg in enumerate(segments, 1):
        out += [f"{i}", f"{ts(seg['start'])} --> {ts(seg['end'])}", seg['text'].strip(), ""]
    return "\n".join(out)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("audio", help="Path to WAV/MP3/FLAC/etc.")
    ap.add_argument("-m","--model", default="medium.en",
                    help="tiny|base|small|medium|large|*.en (default: medium.en)")
    ap.add_argument("-l","--language", default="en", help="ISO code or 'auto'")
    ap.add_argument("--task", default="transcribe", choices=["transcribe","translate"])
    ap.add_argument("--out", default=None, help="Output basename (default: audio filename sans ext)")
    g = ap.add_mutually_exclusive_group()
    g.add_argument("--fp16", dest="fp16", action="store_true", help="Force FP16")
    g.add_argument("--no-fp16", dest="fp16", action="store_false", help="Force FP32")
    ap.set_defaults(fp16=None)
    args = ap.parse_args()

    audio = args.audio
    base = args.out or os.path.splitext(os.path.basename(audio))[0]
    device = "cuda" if torch.cuda.is_available() else "cpu"
    fp16 = args.fp16 if args.fp16 is not None else (device == "cuda")

    model = whisper.load_model(args.model, device=device)
    kw = dict(task=args.task, fp16=fp16)
    if args.language != "auto":
        kw["language"] = args.language

    t0 = time.time()
    result = model.transcribe(audio, **kw)
    dur = time.time() - t0

    with open(base + ".json", "w", encoding="utf-8") as f:
        json.dump(result, f, ensure_ascii=False, indent=2)
    with open(base + ".srt", "w", encoding="utf-8") as f:
        f.write(to_srt(result["segments"]))

    print(f"[{args.model} | {device} | fp16={fp16}] {audio} -> {base+'.srt'} ({dur:.1f}s)")

if __name__ == "__main__":
    main()

