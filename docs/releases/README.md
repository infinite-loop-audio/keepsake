# Releases

Release docs define what ships, how it installs, and what support posture the
published artifacts actually claim.

## Files

- [`v0.1-alpha.md`](v0.1-alpha.md) — published release notes, artifact plan,
  and install surface for the first public alpha
- [`v0.1-alpha-validation-matrix.md`](v0.1-alpha-validation-matrix.md) —
  release-window support matrix and evidence posture
- [`v0.1-alpha-publish-checklist.md`](v0.1-alpha-publish-checklist.md) —
  completed publication gate for the first alpha
- [`../logs/templates/release-checkpoint-template.md`](../logs/templates/release-checkpoint-template.md) —
  one dated closeout log template for the actual release cut

## Release Ops

- dry-run package the current alpha candidate locally with:
  - `effigy release:candidate:alpha`
- local dry-run output path:
  - `dist/v0.1-alpha/`
- build publishable release artifacts in GitHub Actions with:
  - `.github/workflows/release-binaries.yml`
- treat CI-built artifacts as the release source of truth
- leave one dated release checkpoint log when the tag is cut
- once published, treat the release page plus checkpoint log as the authority

## Rule

Do not publish from chat memory alone. Releases should be backed by:

- the active roadmap lane
- current known-issues posture
- current validation evidence
- current changelog/version surfaces
- one dated release checkpoint log when the tag is actually cut

## Next Task

Use the published `v0.1-alpha` release and checkpoint log as the authority, and
route follow-up through `g03` stabilization rather than reopening release-cut
work.
