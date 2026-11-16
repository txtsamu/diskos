# Domain Docs

How the engineering skills should consume this repo's domain documentation.

## Before exploring, read these

- `GLOSSARY.md` at the repo root.
- `docs/adr/`: read ADRs that touch the area you are about to work in.

If a file does not exist, proceed silently. Do not flag its absence or suggest creating it upfront. The `/domain-modeling` skill creates them lazily when terms or decisions actually get resolved.

## Layout

This repo is single-context:

```text
/
├── GLOSSARY.md
├── docs/adr/
│   ├── 0001-working-mode-switches-go-via-local.md
│   └── 0002-usb-debug-is-an-overlay-on-local.md
└── src/
```

## Use the glossary's vocabulary

When your output names a domain concept, use the term as defined in `GLOSSARY.md`. Do not drift to synonyms the glossary explicitly avoids.

If the concept is not in the glossary yet, either you are inventing language the project does not use, or there is a real gap to note for `/domain-modeling`.

## Flag ADR conflicts

If your output contradicts an existing ADR, surface it explicitly rather than silently overriding it.
