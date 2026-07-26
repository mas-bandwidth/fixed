# Contributing to fixed

Pull requests are welcome. If a change is good and useful, we will merge it.

`fixed` is extracted from the fixed-point core of
[box3d](https://github.com/erincatto/box3d) by Erin Catto, and box3d does not take pull
requests. That is Erin's call for his project, and a reasonable one. It is not our policy
here.

You do not need permission to open a pull request. For a bug fix or a small improvement,
just send it. For something large, a new type family, a change to the public API, a
different internal representation, open an
[issue](https://github.com/mas-bandwidth/fixed/issues) first so we can agree on the
approach before you spend a weekend on it.

## Determinism is the whole point

This library exists so the same inputs produce the same bits on every platform,
compiler, and architecture. That constraint decides most questions here:

- No floating point in anything that computes a result. `float` and `double` appear only
  at the boundaries, in the conversion helpers marked for rendering, logging, and
  tooling, and in one place inside `fixISqrt128High` where a hardware `sqrt` seeds an
  integer loop that repairs the answer to the exact floor. That is the bar for any new
  use: exactly repaired, or not in the result path at all. `libm` is not used by the
  library; the tests link it only as an accuracy oracle.
- No undefined behavior. Signed overflow and negative shifts that happen to agree today
  diverge on the next compiler, so the library and its tests are kept UB-free and CI
  enforces it under UBSan.
- The determinism tests assert **frozen hashes**. If your change alters a result, the
  test fails on purpose. Correcting a genuinely wrong result is a legitimate change, but
  say so in the pull request and explain why the old value was wrong. Do not update a
  frozen hash to make a red test go green.

## Opening a pull request

1. Fork the repository and branch from `main`.
2. Keep the pull request to one logical change. If you find yourself touching unrelated
   things, split them into separate pull requests.
3. Say in the description what the change does and why.
4. Build and test locally before you send it:

   ```bash
   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
   cmake --build build
   ctest --test-dir build --output-on-failure
   ```

   On Windows, configure with `-T ClangCL`: the core needs `__int128`, so clang, gcc, or
   clang-cl are supported and plain MSVC is not, by design.

   Then make sure CI is green on the pull request. It runs the suite on Linux, macOS, and
   Windows, and every platform has to produce identical hashes, plus a UBSan job.
5. Add or extend a test when you change behavior. New behavior without a test is not
   done.
6. Match the surrounding code: C, the `fix` / `FIX_` prefix on every public symbol, and
   the naming split between scalar (`fixMul`) and componentwise vector (`fixVecMul`)
   operations. Please do not mass-reformat files you are not otherwise changing, because
   it buries the real change.

## Contributor Assignment Agreement

Before your first contribution can be merged you need to sign the
[Contributor Assignment Agreement](https://github.com/mas-bandwidth/.github/blob/main/CAA.md).
A bot posts the link on your first pull request. Signing is one comment, it takes a
minute, and it covers everything you contribute to any Más Bandwidth repository after
that.

Read it before you sign. It is a copyright assignment, a stronger grant than the license a
CLA usually asks for: you assign copyright in your contribution to Más Bandwidth LLC, and
you get back a perpetual license to use your own work for any purpose. We ask for it so
these libraries can be relicensed in future without tracking down every past contributor
for permission. If your contribution includes third-party material, identify it when you
submit; that material stays under its own license.

The library is offered under MIT, and the box3d-derived material stays MIT in every case.
See [NOTICE](NOTICE) for the provenance and the current licensing position.

## Credit

Contributors keep their name in the commit history, and a change worth calling out gets
called out.

## Questions

Open an [issue](https://github.com/mas-bandwidth/fixed/issues).
