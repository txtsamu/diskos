# Development and recovery

## Branches

- `main`: approved releases. Merge tested work from `dev` only with owner approval.
- `dev`: ongoing work and source checkpoints. A checkpoint may be incomplete;
  say what was checked and what still needs device testing.
- Optional `work/<feature>` branches isolate larger changes before merging into `dev`.

Work against the verified current branch, not a remembered chat version. Preserve
uncommitted work before switching. Push source checkpoints at agreed milestones;
a local commit alone is not a remote backup. Keep `handoff.md` and `todo.md` current.
Keep dev source and its committed payload synchronized when a candidate builds
successfully. Record the source commit, compiler, checksum and pending device-test
status in `payload/mq_ui.build.json`. A commit to dev does not approve a release.
Do not force-push shared branches. Main branch protection is recommended; it has
not been configured by this setup.

## What belongs where

| Material | Durable home |
| --- | --- |
| UI source, scripts, docs, tests, recovery notes | `dev` commits on GitHub |
| Candidate source and matching `payload/mq_ui`, with build metadata | `dev` |
| Approved source and shipping payload | `main` and release tags |
| Test `mq_ui`, source archive, checksums, manifest | Explicitly labelled development prerelease assets |
| Complete verified MIPS toolchain archive, license/source information, checksum | Separate toolchain release assets |
| Owner signing private keys | Owner's machine only; never git or release assets |
| FiiO firmware/rootfs or generated firmware images | Owner's machine only; do not redistribute |

Keep large compiler archives out of git history. GitHub Release assets are the
durable binary backup; Actions artifacts and caches may expire. A branch by itself
does not restore a compiler or a missing compiled candidate.

## Checkpoint contents

For every test build, preserve the full source commit, candidate version, compiled
`mq_ui`, SHA-256, compiler identity and ABI, build instructions, source archive,
and verification results. Mark device tests pending until the owner reports them.
If source is incomplete, preserve a patch against its exact base and label it
recovery-only rather than putting it into the current UI.

Before promising a build in a new environment:

1. Fetch `dev`; verify HEAD, dirty files, and `ui/version.h`.
2. Download the prebuilt compiler from the repository release `toolchain-gcc11.2`: `mipsel-n2008-musl-cross-gcc11.2.0-musl1.2.6-linux-x86_64.tar.xz` and its `.sha256` asset. Verify its checksum and archive integrity.
3. Verify compiler, linker, headers, musl libraries and CRT objects are present.
4. Compile and link a small static MIPS program; verify MIPS32r2, FP64, NaN2008.
5. Read the handoff, then build and run the checks appropriate to the change.

Do not claim this restoration has happened solely because files have familiar names.

## Release discipline

Obtain owner approval and device test feedback, update the displayed Disco version,
docs, website and shipping payload, then merge to `main` and create the approved
version tag. The existing release workflow runs on `v*` tags, not ordinary branch
pushes. Use a separate tag naming convention for development/toolchain backups so
they do not accidentally invoke the stable payload packaging workflow.

Install UI test builds through the existing signed SD workflow. Keep signature
verification, trial acceptance and rollback intact. Development uploads never
contain private keys or stock firmware.

Current candidate: diskOS Disco! **1.4.0 replacement**. See docs/releases/v1.4.0.md for the full scope, pending device acceptance and optional app installation. Disco Options > Layout contains Progress Shape; the theme picker starts with Disco.
