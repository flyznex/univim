# vi-rs vs. libunikey — Vietnamese IME Engine Evaluation

## Goal

Evaluate whether replacing svim's current Vietnamese Telex/VNI engine
(`lib/libunikey`, wrapped by `src/vn_engine.c`) with the Rust crate
[`vi`](https://github.com/ZeroX-DG/vi-rs) ("vi-rs") is worth pursuing.
Research-only: this document records findings and a recommendation, no
implementation is planned as a result.

## Non-goals

- Not a decision to switch engines or start a port.
- Not evaluating LTO for svim generally — only whether it's relevant to the
  VN engine choice specifically (see Performance/LTO below).

## Background

svim vendors libunikey (GPL-3.0, same license as svim) as a git submodule,
per the original [Vietnamese IME design doc](2026-07-24-vietnamese-ime-design.md#non-goals),
which explicitly chose not to reimplement the Telex/VNI rule engine.
`src/vn_engine.c` wraps libunikey's incremental, stateful API
(`UnikeyFilter`, `UnikeyBackspaces`, `UnikeyBuf`) and layers a `word_history`
byte-tracking buffer on top to convert libunikey's byte-count backspace
signal into a character count — a workaround discovered empirically (see the
comments in `vn_engine.c` lines 16-31 and the wedge-bug guard at lines
116-127).

## Findings

### 1. Maturity & correctness confidence

libunikey is the engine behind Unikey, the de facto standard Vietnamese IME
on Windows, in production for roughly two decades with a huge real-world
user base exercising its edge cases. vi-rs (159 GitHub stars, last push
2026-06-22) is actively maintained but pre-1.0 — it has already deprecated
its original `telex`/`vni` module API in favor of a newer
`methods::IncrementalBuffer` API, meaning the public surface has churned
once already. Only 2 open issues, neither about correctness. Reasonable
engineering hygiene, but far less real-world mileage than libunikey.

### 2. Telex/VNI rule coverage

vi-rs has snapshot tests (`tests/telex.rs`, `tests/vni.rs`) driven by a
corpus of ~95 base syllable roots (`testdata/input/all_telex.txt`) with
programmatically generated tone/modifier variations, plus a separate
`non_vietnamese_telex.txt` corpus for foreign/borrowed-word passthrough
behavior. Organized and reasonable, but a much smaller and less
battle-tested surface than what Unikey/libunikey has effectively had
verified by mass real-world usage.

### 3. Architecture fit

vi-rs's `methods::IncrementalBuffer` (`push(char)`, `view() -> &str`,
`clear()`) is per-keystroke and stateful — a good match for svim's existing
call pattern, and closer to libunikey's model than the older
whole-buffer-transform API. However, `view()` only returns the *full current
output string*, not a delta. svim would still need to diff the new `view()`
against the last-displayed text to compute a backspace count + insert
text — structurally the same job `word_history` does today. Switching
engines relocates this glue code rather than eliminating it: instead of
decoding libunikey's opaque byte-count backspace signal (reverse-engineered
empirically, per the existing comments), svim would diff two known strings
(simpler to reason about and unit-test as a plain common-prefix comparison),
but it is still new code requiring its own verification, not a net
reduction in engineering work.

vi-rs also exposes a configurable `AccentStyle` (old vs. new tone-mark
placement, e.g. `hòa` vs. `hoà`) that libunikey's wrapper doesn't currently
surface — a possible nice-to-have, not a driver on its own.

### 4. License

vi-rs is MIT. libunikey is GPL-3.0, matching svim's own license. Since svim
is already GPL-3.0, a more permissive dependency license provides no
practical benefit here.

### 5. Build complexity

This is the largest concrete cost of switching. svim's build is 100%
C/Objective-C via `make` + `clang`, producing universal binaries by
compiling separately for `x86_64-apple-macos12.0` / `arm64-apple-macos12.0`
and combining with `lipo`. Adopting vi-rs would require:

- Adding a Rust toolchain (rustup/cargo) as a build dependency.
- `cargo build --release --target x86_64-apple-darwin` and
  `--target aarch64-apple-darwin` (both native Apple triples, no special
  linker config needed), then `lipo`-combining the resulting static libs,
  mirroring the existing per-arch make targets.
- A hand-written `extern "C"` FFI shim crate (~100-150 lines) exposing
  `push`/`view`/`clear`/`reset` across the boundary — vi-rs has no existing
  C bindings.
- Ongoing exposure to vi-rs's pre-1.0 API churn on that shim.

### 6. Performance / LTO

Not a relevant axis for this decision. Both engines process a single
keystroke in nanoseconds-to-low-microseconds; human typing speed is many
orders of magnitude slower. There is no measured or plausible bottleneck in
the VN engine to fix, so LTO has nothing to optimize here regardless of
which engine is used.

## Recommendation

**Keep libunikey.** Its verified real-world correctness outweighs vi-rs's
marginally cleaner architecture, given the real cost of switching (new
Rust toolchain in the build, a hand-written FFI shim, and re-verifying every
Telex/VNI edge case svim currently trusts) against a license benefit that
doesn't apply here and a performance angle that isn't a real problem.

Revisit only if:
- A concrete correctness bug is found in libunikey that vi-rs demonstrably
  handles correctly, or
- vi-rs reaches 1.0 (reducing API-churn risk for a hand-maintained FFI
  shim).
