# nova-tools changelog

## v1.6.0 — 2026-11-27

Cut from d537f4e9c9ef2061734a28cd1c0aaf66900c1646. 5 pull requests since v1.5.0.

Dogfood gate waived: fixed is a C math library, not a nova tool: it has no nova CLI reference or dogfood receipts, and no sprint spend store or provider spend

Spend gate waived: fixed is a C math library, not a nova tool: it has no nova CLI reference or dogfood receipts, and no sprint spend store or provider spend (window 2026-10-27T00:00:00Z..2026-11-27T17:17:00Z)
- unread: --spend-store <addr> names no sprint store, so the recorded spend cannot be read

- #67 changelog: the v1.5.0 cut record
- #65 ci: Dependabot pull requests skip the PR-author check
- #68 Wide and time follow fixed_t's overflow rule; fixWideMul and fixWideDiv
- #69 ci: a wasm32 leg (Emscripten 6.0.13, run under node)
- #63 Strict warnings as errors by default; clear the 35 in the tests

## v1.5.0 — 2026-10-27

Cut from 8dda4fcdae38a7fde0a63bd9ba8edc3f2f75ad1c. 14 pull requests since v1.4.0.

Dogfood gate waived: fixed is a C math library, not a nova tool: it has no nova CLI reference or dogfood receipts, and no sprint spend store or provider spend

Spend gate waived: fixed is a C math library, not a nova tool: it has no nova CLI reference or dogfood receipts, and no sprint spend store or provider spend (window 2026-08-25T00:00:00Z..2026-10-27T23:31:21Z)
- unread: --spend-store <addr> names no sprint store, so the recorded spend cannot be read

- #26 README: the CI badge
- #27 Support line: link to Patreon membership
- #28 Fix numerical boundary contracts and optimize normalization and bounds
- #39 fixComputeCosSinPrecise and fixMulDiv for the Virtual Go stone
- #40 Fix windows-clangcl link: gate native __int128 test references
- #56 roadmap: create docs/roadmap.sexp, defer the fixed-core waiting cards (Glenn 2026-10-19)
- #57 roadmap: carry the 1 unlanded sprint card(s) out of the sprint (Glenn 2026-10-19)
- #58 clang-cl takes the GNU always_inline attribute, not __forceinline
- #59 fixDot saturates at the top of the range under FIX_SATURATE
- #60 fixVecAdd, fixVecSub and fixCross saturate under FIX_SATURATE
- #61 fixDotQuat, fixMulQuat and fixInvMulQuat saturate under FIX_SATURATE
- #62 Every remaining fixed_t add and subtract saturates under FIX_SATURATE
- #64 caa: allowlist logins only (gafferongames,rowan-claude)
- #66 Unsigned negation is spelled 0 - x; plain MSVC builds at /W4 /WX

