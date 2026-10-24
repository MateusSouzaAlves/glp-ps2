# Neutral CHD decoder

<!-- doc-status: legal; reviewed: 2026-10-24 -->

The product uses a standalone positional CHD decoder compiled from the pinned
Play!/Framework/libchdr/codec sources. It does not use the retired Play! VM.
The original licenses remain in those public pinned upstreams. No Sony BIOS,
firmware or game is needed to compile the decoder.

Fetch `https://github.com/jpd002/Play-.git` at the revision and all recursive
dependency revisions in `config/ps2-core.lock.json`, preserving their notices.
Run `python -B scripts/build-public-decoder.py --source <pinned-Play-source>
--build <fresh-external-build> --sdk <Android-SDK>` with installed NDK/CMake.
The script accepts `--configure-only` for a source/dependency configuration
check; that result must not be reported as a completed binary build.

ARMSX2 remains the PS2 VM. This decoder recipe has no private-app dependency
and deliberately does not import the old mixed VM build spool.
