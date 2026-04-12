# Contracts

Use this folder for explicit non-code contracts that constrain behavior.

## Files

- [`contract-index.md`](contract-index.md) — register of all active contracts
- [`001-working-rules.md`](001-working-rules.md) — execution grammar, done-ness, and autonomy rules

## Rule

Contracts should be stable reference artifacts and link to relevant roadmap/log
evidence. Roadmap work should not proceed until the required contract exists and
is listed in the contract index.

Contracts are the hard-definition surface for behavior, interfaces, policies,
and other durable rules that should not live only in provisional specs.

For execution work in this repo, `001-working-rules.md` is the anchor contract.
Start there.

## Next Task

Use g02 to decide whether the platform config and release-surface boundaries
need additional contracts before `v0.1-alpha` packaging and publication.
