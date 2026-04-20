# G03.001 — Windows Release Claim Correction

Date: 2026-04-20
Milestone: `g03.001`
Status: complete

## Why this exists

The published `v0.1-alpha` docs froze Windows around the earlier exploratory
`test-plugin.dll` lane. That was an honest release-time snapshot, but it no
longer matches the stronger Windows evidence we gathered afterward in the VM.

The release docs should still keep Windows out of the primary supported claim,
but they should stop implying that the only real-host proof is "scan/add/UI on
repo `test-plugin.dll`".

## Windows Evidence Now Worth Claiming

Environment:

- Windows 11 ARM64 VM
- REAPER x64
- installed Keepsake CLAP bundle under `C:\Program Files\Common Files\CLAP`

Representative non-repo plugins validated through Keepsake:

- APC x64
- APC x86
- Serum x64
- Serum x86

What was proven in this VM lane:

- discovery / scan in REAPER
- instantiate / add FX
- embedded UI open / close
- short transport / audio proof for APC and Serum
- stable Windows embedded-editor posture after the staged/deferred open fix
- cross-architecture coexistence:
  - APC x64
  - APC x86
  - Serum x64
  - Serum x86
  - all four loaded together in one REAPER session
  - all four UIs opened
  - transport ran with audio present
  - no host lockup in the multi-plugin smoke lane

## What This Changes

Windows should still remain an explicitly experimental public lane for
`v0.1-alpha`, because:

- macOS + REAPER + VST2 is still the strongest packaged release proof
- the Windows evidence is still concentrated in a Windows 11 ARM64 VM with
  REAPER x64 rather than a broader host matrix
- the post-release evidence was finalized after the original release docs froze

But the docs should now say:

- Windows has real-host REAPER VM evidence with APC and Serum
- Windows x64 and x86 VST2 variants were both exercised
- 32-bit on Windows is no longer "no release-window proof at all"
- the remaining Windows caveats are breadth/support posture, not "only repo
  `test-plugin.dll` has ever been proven"

## Practical Release Read

For `v0.1-alpha`, the honest current posture is:

- supported:
  - macOS + REAPER + VST2
- experimental but materially validated in REAPER:
  - Windows VST2 on the Windows 11 ARM64 VM lane, including APC/Serum x64+x86
- experimental with weaker release proof:
  - Linux
  - VST3
  - AU v2

## Next Task

Update the release-facing docs (`v0.1-alpha.md`,
`v0.1-alpha-validation-matrix.md`, and `known-issues-v0.1-alpha.md`) so the
Windows section reflects the stronger APC/Serum x64+x86 evidence without
promoting Windows into the primary supported claim.
