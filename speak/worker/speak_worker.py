"""
Backend for the `speak` tiny app: a line-protocol wrapper around mlx-audio.

    speak_worker.py serve            persistent worker; commands on stdin
    speak_worker.py download <repo>  one-shot model download with progress

Every protocol line is tab separated and goes to the real stdout; anything
mlx-audio prints is redirected to stderr so it can't corrupt the protocol.

Commands (serve):
    models                                  -> model\t<repo>\t<local 0|1> ... models_end
    info\t<repo>                            -> info\t<repo>\t<local>\t<bytes>\t<voices,>\t<code=name,>
    gen\t<id>\t<repo>\t<voice>\t<lang>\t<out.wav>\t<escaped text>
                                            -> progress\t<id>\t<0..1>\t<stage> ... [note\t<id>\t<msg>]
                                               done\t<id>\t<path>\t<secs>
    A new `gen` cancels one in flight (reported as cancelled\t<id>). Text
    longer than the model's MAX_CHARS is generated in stages split at
    paragraphs (stage names "Part i of n"), into one file.
Errors: error\t<id or command>\t<message>. Text escapes: \\n, \\t, \\\\.
"""

import os
import re
import sys
import threading
import time
import queue
import wave
from pathlib import Path

# Protocol channel = the original stdout; everything else prints to stderr.
_proto = os.fdopen(os.dup(1), "w", buffering=1, encoding="utf-8")
os.dup2(2, 1)
sys.stdout = sys.stderr
_proto_lock = threading.Lock()


def emit(*fields):
    line = "\t".join(str(f).replace("\t", " ").replace("\n", " ") for f in fields)
    with _proto_lock:
        _proto.write(line + "\n")
        _proto.flush()


# Kept in step with mlx_audio.utils.DEFAULT_ALLOW_PATTERNS, so a download here
# fetches exactly what load_model() would.
ALLOW = ["*.json", "*.safetensors", "*.py", "*.model", "*.tiktoken", "*.txt",
         "*.jinja", "*.jsonl", "*.yaml", "*.npz", "*.pth"]

CURATED = [
    "mlx-community/Kokoro-82M-bf16",
    "mlx-community/Kokoro-82M-8bit",
    "mlx-community/Kokoro-82M-6bit",
    "mlx-community/Qwen3-TTS-12Hz-0.6B-CustomVoice-8bit",
    "mlx-community/Qwen3-TTS-12Hz-0.6B-CustomVoice-bf16",
    "mlx-community/Qwen3-TTS-12Hz-1.7B-CustomVoice-6bit",
]

KOKORO_LANGS = [("a", "English (US)"), ("b", "English (UK)"), ("e", "Spanish"),
                ("f", "French"), ("h", "Hindi"), ("i", "Italian"), ("j", "Japanese"),
                ("p", "Portuguese"), ("z", "Chinese")]


def family(repo):
    r = repo.lower()
    if "kokoro" in r:
        return "kokoro"
    if "qwen3-tts" in r and "customvoice" in r:
        return "qwen3"
    return None


def hub_cache():
    from huggingface_hub import constants
    return Path(constants.HF_HUB_CACHE)


def repo_dir(repo):
    return hub_cache() / ("models--" + repo.replace("/", "--"))


def partial_files(d):
    """In-flight downloads: <blob>.<id>.incomplete files whose blob isn't
    there yet. (A cancelled download can leave stale ones next to the blob.)"""
    return [p for p in (d / "blobs").glob("*.incomplete")
            if not (d / "blobs" / p.name.split(".")[0]).exists()]


def snapshot(repo):
    """The local snapshot folder if the model is fully downloaded, else None."""
    d = repo_dir(repo)
    snaps = sorted((d / "snapshots").glob("*"), key=lambda p: p.stat().st_mtime, reverse=True)
    if not snaps or partial_files(d):
        return None
    s = snaps[0]
    if (s / "config.json").exists() and list(s.glob("*.safetensors")):
        return s
    return None


def cmd_models():
    seen = []
    for repo in CURATED:
        seen.append(repo)
    # Plus any other supported model already in the cache.
    for d in sorted(hub_cache().glob("models--*")):
        repo = d.name[len("models--"):].replace("--", "/", 1)
        if repo not in seen and family(repo) and snapshot(repo):
            seen.append(repo)
    for repo in seen:
        emit("model", repo, 1 if snapshot(repo) else 0)
    emit("models_end")


def read_config(repo, snap):
    import json
    if snap:
        return json.loads((snap / "config.json").read_text())
    from huggingface_hub import hf_hub_download
    # Into a scratch dir, so the cache doesn't look half-downloaded.
    import tempfile
    p = hf_hub_download(repo, "config.json", local_dir=tempfile.mkdtemp(prefix="speak-cfg-"))
    return json.loads(Path(p).read_text())


def remote_size(repo):
    from fnmatch import fnmatch
    from huggingface_hub import HfApi
    info = HfApi().model_info(repo, files_metadata=True)
    return sum(s.size or 0 for s in info.siblings
               if any(fnmatch(s.rfilename, pat) for pat in ALLOW))


def cmd_info(repo):
    fam = family(repo)
    snap = snapshot(repo)
    size = 0 if snap else remote_size(repo)
    if fam == "kokoro":
        if snap:
            voices = sorted(p.stem for p in (snap / "voices").glob("*.safetensors"))
        else:
            from huggingface_hub import list_repo_files
            voices = sorted(Path(f).stem for f in list_repo_files(repo)
                            if f.startswith("voices/") and f.endswith(".safetensors"))
        langs = KOKORO_LANGS
    elif fam == "qwen3":
        cfg = read_config(repo, snap)
        talker = cfg.get("talker_config", {})
        voices = sorted(talker.get("spk_id", {}) or ["default"])
        names = [n for n in talker.get("codec_language_id", {}) if "dialect" not in n]
        langs = [("auto", "Auto")] + [(n, n.title()) for n in sorted(names)]
    else:
        raise ValueError(f"unsupported model family: {repo}")
    emit("info", repo, 1 if snap else 0, size, ",".join(voices),
         ",".join(f"{c}={n}" for c, n in langs))


# The most text one generate() call should get. Kokoro phonemizes at most 510
# tokens per chunk (about 400 characters of English); Qwen3-TTS stops at 2048
# codec tokens, about 160 s at 12.5 Hz, so stay well under that.
MAX_CHARS = {"kokoro": 400, "qwen3": 1200}

SENTENCE = re.compile(r"(?<=[.!?。！？])\s+")
PARAGRAPH = re.compile(r"\n\s*\n")


def pack(pieces, limit, sep):
    """Greedily joins pieces into runs of at most `limit` characters."""
    out, cur = [], ""
    for p in pieces:
        if cur and len(cur) + len(sep) + len(p) > limit:
            out.append(cur)
            cur = p
        else:
            cur = cur + sep + p if cur else p
    if cur:
        out.append(cur)
    return out


def stages(text, limit):
    """Text within the limit is one stage. Longer text splits at paragraphs,
    packed up to the limit; an overlong paragraph splits at sentences, and an
    overlong sentence at words."""
    text = text.strip()
    if len(text) <= limit:
        return [text] if text else []
    pieces = []
    for para in (p.strip() for p in PARAGRAPH.split(text)):
        if not para:
            continue
        if len(para) <= limit:
            pieces.append(para)
            continue
        sents = []
        for sent in SENTENCE.split(para):
            if len(sent) <= limit:
                sents.append(sent)
            else:
                sents += pack(sent.split(), limit, " ")
        pieces += pack(sents, limit, " ")
    return pack(pieces, limit, "\n\n")


class Cancelled(Exception):
    pass


_loaded = {"repo": None, "model": None}


def get_model(repo):
    if _loaded["repo"] != repo:
        _loaded["model"] = None  # free the old one first
        from mlx_audio.tts.utils import load_model
        _loaded["model"] = load_model(repo)
        _loaded["repo"] = repo
    return _loaded["model"]


def write_wav(path, chunks, sr):
    import numpy as np
    audio = np.concatenate([np.asarray(c, dtype=np.float32).reshape(-1) for c in chunks])
    pcm = (np.clip(audio, -1.0, 1.0) * 32767.0).astype("<i2")
    tmp = str(path) + ".part"
    with wave.open(tmp, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(sr)
        w.writeframes(pcm.tobytes())
    os.replace(tmp, path)
    return len(audio) / sr


_skipped = []  # words the current job's pipeline dropped


def kokoro_pipeline(model, lang):
    """Kokoro's pipeline for `lang`, made to skip words it can't pronounce.

    Without espeak-ng, misaki's English G2P has no fallback for out-of-
    vocabulary words and crashes on them; a fallback that returns no phonemes
    drops the word instead, and we report which ones were dropped."""
    name = dict(KOKORO_LANGS).get(lang, lang)
    try:
        pipe = model._get_pipeline(lang)
    except RuntimeError as e:
        if "espeak" in str(e).lower():
            raise ValueError(f"{name} needs espeak-ng: brew install espeak-ng") from None
        raise
    except ImportError:
        # Chinese and Japanese G2P live in optional misaki extras.
        extra = {"z": "zh", "j": "ja"}.get(lang)
        pkg = f'"misaki[{extra}]"' if extra else "misaki"
        raise ValueError(f"{name} needs more Python packages: pip install {pkg}") from None
    g2p = getattr(pipe, "g2p", None)
    if g2p is not None and hasattr(g2p, "fallback") and g2p.fallback is None:
        def skip(token):
            _skipped.append(token.text)
            return "", 1
        g2p.fallback = skip  # the pipeline is cached, so this outlives the job
    return pipe


def cmd_gen(job, repo, voice, lang, out, text, cancel):
    if not snapshot(repo):
        raise ValueError("model is not downloaded")
    os.environ["HF_HUB_OFFLINE"] = "1"  # never fetch behind the user's back
    emit("progress", job, 0.02, "Loading model")
    model = get_model(repo)
    _skipped.clear()
    if family(repo) == "kokoro":
        kokoro_pipeline(model, lang)
    parts = stages(text, MAX_CHARS.get(family(repo), 400))
    total = sum(len(p) for p in parts) or 1
    done_chars = 0
    chunks, sr = [], getattr(model, "sample_rate", 24000)
    stream = family(repo) != "kokoro"  # Kokoro is fast; others stream within a stage
    for i, p in enumerate(parts):
        if cancel.is_set():
            raise Cancelled()
        base = 0.1 + 0.85 * done_chars / total
        span = 0.85 * len(p) / total
        stage = f"Part {i + 1} of {len(parts)}" if len(parts) > 1 else "Speaking"
        emit("progress", job, f"{base:.3f}", stage)
        kw = dict(text=p, voice=voice, lang_code=lang)
        if stream:
            kw.update(stream=True, streaming_interval=1.0)
        expect = max(1.0, len(p) / 14.0)  # rough speech rate, chars per second
        got = 0.0
        for r in model.generate(**kw):
            if cancel.is_set():
                raise Cancelled()
            chunks.append(r.audio)
            sr = getattr(r, "sample_rate", sr) or sr
            got += r.audio.shape[-1] / sr
            emit("progress", job, f"{base + span * min(0.95, got / expect):.3f}", stage)
        done_chars += len(p)
    if not chunks:
        raise ValueError("model produced no audio")
    emit("progress", job, 0.97, "Writing")
    secs = write_wav(out, chunks, sr)
    if _skipped:
        words = ", ".join(dict.fromkeys(_skipped))
        emit("note", job, f"skipped {words} (needs espeak-ng)")
    emit("done", job, out, f"{secs:.2f}")


def unescape(s):
    return re.sub(r"\\(.)", lambda m: {"n": "\n", "t": "\t"}.get(m.group(1), m.group(1)), s)


def serve():
    jobs = queue.Queue()
    cancel = threading.Event()

    def reader():
        for line in sys.stdin:
            f = line.rstrip("\n").split("\t")
            if f[0] == "gen":
                cancel.set()  # a newer request supersedes the one in flight
            jobs.put(f)
        jobs.put(None)

    threading.Thread(target=reader, daemon=True).start()
    emit("ready")
    while True:
        f = jobs.get()
        if f is None:
            return
        try:
            if f[0] == "models":
                cmd_models()
            elif f[0] == "info":
                cmd_info(f[1])
            elif f[0] == "gen":
                # Only the newest queued gen matters.
                if any(q[0] == "gen" for q in list(jobs.queue) if q):
                    emit("cancelled", f[1])
                    continue
                cancel.clear()
                cmd_gen(f[1], f[2], f[3], f[4], f[5], unescape(f[6]), cancel)
            else:
                emit("error", f[0], "unknown command")
        except Cancelled:
            emit("cancelled", f[1])
        except Exception as e:  # report and keep serving
            if not isinstance(e, ValueError):
                import traceback
                traceback.print_exc()
            key = f[1] if f[0] in ("gen", "info") and len(f) > 1 else f[0]
            # ValueErrors are already worded for the user; anything else is a bug.
            msg = str(e) if isinstance(e, ValueError) else f"{f[0]} failed ({type(e).__name__}: {e})"
            emit("error", key, msg[:300])


def dir_bytes(d):
    n = 0
    if not d.exists():
        return 0
    for p in list(d.iterdir()):
        try:
            if p.is_file() and not p.is_symlink():
                n += p.stat().st_size
        except OSError:
            pass
    return n


def download(repo):
    from huggingface_hub import snapshot_download
    try:
        total = remote_size(repo)
        d = repo_dir(repo)
        result = {}

        def run():
            try:
                result["path"] = snapshot_download(repo, allow_patterns=ALLOW)
            except Exception as e:
                result["error"] = e

        t = threading.Thread(target=run, daemon=True)
        t.start()
        # Files land in blobs/ (as .incomplete while in flight). Sizes dip
        # briefly while files are renamed, so progress only moves forward.
        shown = 0
        while t.is_alive():
            shown = max(shown, min(dir_bytes(d / "blobs"), total))
            emit("dl", repo, shown, total)
            t.join(0.25)
        if "error" in result:
            raise result["error"]
        for p in (d / "blobs").glob("*.incomplete"):  # leftovers of cancelled runs
            p.unlink(missing_ok=True)
        emit("dl", repo, total, total)
        emit("dldone", repo)
    except Exception as e:
        emit("error", "dl", f"{type(e).__name__}: {e}"[:300])
        sys.exit(1)


if __name__ == "__main__":
    if len(sys.argv) >= 3 and sys.argv[1] == "download":
        download(sys.argv[2])
    else:
        serve()
