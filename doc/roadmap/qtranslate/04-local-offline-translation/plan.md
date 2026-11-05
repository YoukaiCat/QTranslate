---
kind: stage
status: blocked
---

# Local offline translation (old stage 14)

## Blocked

Blocked by the user decision on 2026-11-05: the stage is design-only and not sufficiently worked out. The user is not ready to implement Argos Translate/CTranslate2 now; the LLM path through OpenAI-compatible providers (OpenRouter, Ollama, and other local or remote providers) covers the current need. Unblock when the user approves the engine and model licences, the distribution model, the acceptable quality threshold, and the first language pairs.

## Goal

Add an optional local mode with no API keys and no network text transfer: Argos Translate models executed through CTranslate2, with model downloads from the UI and a fallback to cloud services when no model is installed.

## What to do, what is touched

A Python-Argos proof of concept for quality and speed; a native `CTranslate2Runner` for `.argosmodel` without Python; `ArgosModelManager` with download, SHA-256, safe extraction, and deletion; `LocalArgosService` with pivot through English; sentence segmentation and formatting preservation; UI for the local service and model list; fallback to cloud services. The old plan's stage 14 and `doc/local-translation.md` list the details.

## Done when

- The proof of concept shows acceptable quality on en↔ru and at least one pivot pair.
- A model downloads by button, is checked by SHA-256, and runs without Python.
- Local translation does not block the UI and can be cancelled.
- The installer stays compact and models are not bundled.
- Model and engine licences are checked.
- README and architecture describe the limitations.

## Depends on

None technically; follows stage 3 in the old order and is independent.

## Risks and what is needed from the user

Model quality may be below DeepL/Google; models are large and their licences vary; weak CPUs may be slow. The user must approve the model and engine licences, the distribution model, and the acceptable quality threshold. The user should choose the first language pairs to test.
