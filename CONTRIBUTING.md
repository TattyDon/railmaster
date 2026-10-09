# Contributing to Railmaster

Thanks for helping. Please read [docs/CLEAN_ROOM.md](docs/CLEAN_ROOM.md) first.
It sets out what may and may not be committed, and a contribution that breaks
it cannot be accepted.

## The workflow: spec first, then code

Fidelity to the original game is the point of this project, so behaviour is
written down before it is implemented:

1. **Spec.** Add or correct the relevant page in `docs/spec/`. Each rule or
   number needs a source: a URL, a manual page, or an "observed in-game"
   note that says how it was measured. Mark guesses with **UNVERIFIED**.
2. **Implement** in `src/sim/`, referencing the spec section in a comment
   where it isn't obvious.
3. **Test.** Encode the spec's numbers in a unit test under `tests/`.

## Simulation rules

`src/sim` must stay deterministic, because saves, replays and lockstep
multiplayer depend on it:

- No floating point in game state or game logic. Use integers, `Money`
  (cents) and fixed-point helpers.
- Use `railmaster::sim::Random`, never `std::` distributions or `rand()`.
- No reading the clock, no threads, and no iteration over unordered
  containers where the order affects outcomes.
- No rendering, SDL or OS dependencies.

## Building

See the README. Before opening a PR, run:

```sh
cmake --build build && ctest --test-dir build --output-on-failure
```

Format with `clang-format` (the config is in the repo root).
