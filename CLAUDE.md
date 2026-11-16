# diskOS

Read [`agents/AGENTS.md`](agents/AGENTS.md) before changing anything.

**No comments in source.** Never add code comments (`//`, `/* */`, `#`, `<!-- -->`) to code, scripts, Makefiles, tests or web
files. Put explanations in [`comments.md`](comments.md) under the file's heading, or in `docs/`. Keep SPDX/copyright headers,
shebangs and tool directives (`noqa`, `NOLINT`, `/* fall through */`, `shellcheck disable`). Never anchor a test on comment text.
Full rule: `agents/AGENTS.md`, section *No comments in source*.

## Agent skills

### Issue tracker

GitHub Issues at `txtsamu/diskos` (the fork, not upstream). Pass `-R txtsamu/diskos` explicitly. See `docs/agents/issue-tracker.md`.

### Triage labels

Default five roles: `needs-triage`, `needs-info`, `ready-for-agent`, `ready-for-human`, `wontfix`. See `docs/agents/triage-labels.md`.

### Domain docs

Single-context: `GLOSSARY.md` and `docs/adr/` at the repo root. See `docs/agents/domain.md`.
