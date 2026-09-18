## Summary

<!-- What does this change, and why. Link an issue if one exists. -->

## Checklist

- [ ] `make -j8 && make test` passes
- [ ] `make format` produces an empty diff
- [ ] `make debug && make test` (ASan/UBSan) passes
- [ ] CMake Release + `ctest` passes (only required if this touches `CMakeLists.txt`,
      a native decoder backend in `src/loaders/`, or `src/batch.c`)
- [ ] `CHANGELOG.md` has a line under `[Unreleased]`, or this change has no
      consumer-visible effect and doesn't need one
- [ ] If this changes hash output for any existing input: called out explicitly below,
      and filed under `BREAKING CHANGES` in `CHANGELOG.md`

## Does this change any hash output?

<!-- Yes/no. If yes: which algorithm(s), on what kind of input, and why the change is
     correct rather than a regression. -->
