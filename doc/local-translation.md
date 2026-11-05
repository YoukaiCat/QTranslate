# Local offline translation: the Argos / CTranslate2 option

Date: 2026-11-05. Status: **roadmap stage 4 (old stage 14); option, code not implemented**.

This document records a possible local mode of qtranslate without API keys and without sending text
to the internet. The main candidate is Argos Translate models executed through CTranslate2. This is
not an LLM but neural machine translation (NMT).

## 1. Why

- Privacy: the text does not go to the cloud.
- Work without the internet after a model is downloaded.
- No keys, quotas, or dependence on changes in cloud APIs.
- A complement to cloud services: a "local mode" as a fallback or user choice.

Cons: quality lower than DeepL/Google, models take space, on a weak CPU long texts are slower, and
the installer requires a separate engine and model manager.

## 2. Engine options

| Option | What it is | Pros | Cons |
|---|---|---|---|
| **Argos Translate (Python)** | Python library + `argospm` + models | Fast prototype, everything is ready | Cannot be shipped as is: Python, spaCy/Stanza, and dependencies are required |
| **Argos models + CTranslate2 (C++)** | A native runner reads `.argosmodel` | No Python, smaller size, embeds into Qt | We must implement loading/unpacking, segmentation, and the UI ourselves |
| **MetalTranslate** | Experimental C++ template of Argos on CTranslate2 | Shows the minimal native path | Not production-ready: no model manager, segmentation, or out-of-the-box Windows build |
| **OPUS-MT + CTranslate2** | Helsinki-NLP models + CTranslate2 | Many models, a clear format | Model licenses vary; quality and size vary widely |
| **llama.cpp + GGUF LLM** | A local LLM via a prompt | Context, tone, glossaries | 2.5–5 GB per model, slow on CPU, non-deterministic |
| **ONNX Runtime GenAI** | A universal runtime for ONNX models | DirectML/OpenVINO, one runtime for different models | Compatible ONNX models are required; export is harder; fewer ready LLMs |
| **LibreTranslate** | A Python server on top of Argos | A ready HTTP API, self-hosted | AGPL-3.0, Python, heavy; it is a server, not a library |

For qtranslate the recommended path is: **Argos models + CTranslate2**. Use Python Argos only for a
prototype and quality checks.

### 2.1. ONNX Runtime GenAI and llama.cpp

**ONNX** is a model format, not an engine. `.onnx` itself does not run: **ONNX Runtime** is
required. For LLMs on top of it there is **ONNX Runtime GenAI** — a wrapper with a tokenizer, KV
cache, and generation. Plain ONNX Runtime provides only tensors, and tokenization/sampling/KV cache
would have to be written by us.

- **CPU:** yes, ONNX Runtime works on CPU (x86-64/ARM64, AVX2/AVX-512, INT8).
- **Linux:** yes, there are official x64/ARM64 builds and a C++ API; GenAI also builds on Linux.
- **GPU:** CUDA/TensorRT/OpenVINO — Linux and Windows; **DirectML — Windows only**; Metal — macOS.
- **Models:** compatible ONNX exports are required, often multi-file; not every `.onnx` is suitable
  for GenAI.
- **Size:** gigabytes are the model, not the engine; with ONNX and llama.cpp a 4B/7B model weighs
  about the same (2.5–5 GB in INT4/Q4).

**llama.cpp** is a specialized C++ engine for GGUF models. It also embeds, but:

- A GGUF model is one file with the tokenizer and chat template inside;
- There are far more ready GGUF models;
- CPU inference is very well optimized;
- Ready releases exist; for the product the engine is built once in CI, and the user compiles
  nothing;
- `llama-server` can be run as a sidecar with an OpenAI-compatible API.

| | ONNX Runtime GenAI | llama.cpp |
|---|---|---|
| Format | ONNX | GGUF |
| Purpose | general ML + LLM | primarily LLM |
| CPU | yes, MLAS/oneDNN | yes, ggml |
| GPU | CUDA/TensorRT/DirectML/OpenVINO | CUDA/Vulkan/Metal/HIP |
| Linux | yes | yes |
| Ready LLMs | fewer, export needed | very many |
| Quantization | INT8/INT4 with ORT tools | GGUF K-quants |
| Integration | C++ API + DLL/SO + providers | C++ API, often a single binary/library |
| When to choose | DirectML/OpenVINO, an ONNX model already exists, one runtime for everything | a local LLM, CPU-first, simple model distribution |

For qtranslate: **llama.cpp** is the main candidate for a local LLM; **ONNX Runtime GenAI** is an
alternative if DirectML/OpenVINO or a specific ONNX model is needed. For NMT (Argos/OPUS),
**CTranslate2** is still better than ONNX.

## 3. How Argos works

`pip install argostranslate` (version 1.11.0 as of 2026-11) installs:

- `ctranslate2` — inference;
- `sentencepiece`, `sacremoses` — tokenization;
- `spacy`, `stanza==1.10.1`, `minisbd` — sentence segmentation;
- `packaging`.

Models are installed separately:

```bash
argospm update
argospm install translate-en_ru translate-ru_en
argos-translate --from en --to ru "Hello world"
```

The `.argosmodel` model is a zip:

- `model/` — the CTranslate2 model;
- `sentencepiece.model`;
- `metadata.json` (language codes, package version).

CTranslate2 reads `model/`, and SentencePiece handles the tokenizer. The model uses language
prefixes such as `__en__` / `__ru__`: they are added to the tokens before translation and removed
from the result. This is exactly how the minimal
[MetalTranslate](https://github.com/argosopentech/MetalTranslate) (MIT) is built: C++ + CTranslate2
+ OpenNMT Tokenizer.

The Argos model index is [argospm-index](https://github.com/argosopentech/argospm-index). It mostly
contains `X → en` and `en → X` directions; for pairs without a direct model, Argos pivots through
English (`ru → en → de`). This matters for quality and latency.

## 4. What a native mode needs

1. **`ArgosModelManager`**
   - A list of available models from our manifest (URL, SHA-256, size, license, directions);
   - Download with progress, resume, and cancellation;
   - Hash and free-space checks;
   - Safe unpacking of `.argosmodel` into `%LOCALAPPDATA%\qtranslate\models`;
   - Model deletion and disk cleanup.

2. **`CTranslate2Runner`**
   - Loading `model/` and `sentencepiece.model`;
   - Tokenization, language prefixes, decoding;
   - `Translator`/thread pool, request cancellation;
   - Lazy model loading and unloading from memory.

3. **Segmentation**
   - Argos uses Stanza/minisbd; in the native variant — `QTextBoundaryFinder` or our own
     sentence/paragraph splitter;
   - Long text is split into sentences, translated as a batch, and joined back while preserving
     spaces and line breaks.

4. **`LocalArgosService : TranslationService`**
   - `isConfigured()` — whether a model for the pair is installed;
   - Pivot `src → en → dst` if there is no direct model;
   - Errors "model is not installed", "not enough memory", "unsupported language";
   - Translation cache (the shared `TranslationManager` already has it).

5. **UI**
   - The "Services" tab → "Local (offline)";
   - A list of installed and available models;
   - "Download", "Delete" buttons, size and free space;
   - A warning that quality is lower than cloud services;
   - The "Local" choice in the common service list.

## 5. Limitations and risks

- **Quality.** Argos models are older and smaller than DeepL/Google. For en↔ru and European pairs it
  may be acceptable; for other languages it is noticeably worse. A benchmark on real phrases is
  needed.
- **Pivot.** `ru → de` through English means double translation and quality loss.
- **Size.** One direction is usually tens to hundreds of MB; bidirectional mode needs two models.
  The full language matrix cannot be installed.
- **Licenses.** Argos Translate — MIT/CC0, CTranslate2 — MIT, MetalTranslate — MIT. Licenses of
  specific models must be checked separately; NLLB/Seamless, for example, are CC-BY-NC and are not
  suitable for commercial distribution without checking.
- **Security.** `.argosmodel` is a zip from the internet: path traversal protection, SHA-256
  verification, and downloads only from trusted URLs are needed.
- **Python path.** If a prototype is made with Argos Python, we cannot assume the user has
  Python/pip; it is a dev tool only.

## 6. PoC

1. Locally: `pip install argostranslate`, `argospm update`, install `en↔ru`, `en↔de`, `en↔zh`.
2. Run 20–30 real qtranslate phrases: short ones, long paragraphs, formatting, names, and terms.
3. Measure: quality, time for 100/1000/5000 characters, RAM, model size, and behaviour on a CPU
   without a GPU.
4. Check the pivot `ru → de` and `ru → zh`.
5. If the quality is acceptable — build a native CTranslate2 runner; if not — keep Argos as an
   experimental mode or move to llama.cpp/cloud.

## 7. Decision

Argos is a good **first candidate** for offline mode, but not through `pip install` in the
distribution. The right path:

- A Python prototype — for checking quality and speed;
- Native CTranslate2 + `.argosmodel` — for the product;
- llama.cpp/LLM — a separate, heavier option if context and tone are needed;
- LibreTranslate — only as an external self-hosted server, considering AGPL.
