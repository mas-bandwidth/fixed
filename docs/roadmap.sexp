; The fixed roadmap: the data ROADMAP.md is generated from.
; Same shape as nova-sprint's docs/roadmap.sexp (internal/roadmapdoc's package comment):
; data, never evaluated. Edit this file, then regenerate ROADMAP.md with nova-sprint's
; generator, from a nova-sprint checkout:
;   go build -o /tmp/roadmap ./tools/roadmap && (cd <this repo> && /tmp/roadmap --file docs/roadmap.sexp --out ROADMAP.md)
; Never edit ROADMAP.md by hand. Created 2026-10-14 when the open sprint cards of this
; repository were moved out of the stopped sprint.
(roadmap "v1"
 :title "fixed roadmap"
 :text "Planned work for fixed. Each item below was an open sprint card, moved out of the stopped sprint on 2026-10-14; it names its card, its tier, its base branch and any pushed work to resume from. Landed work is in git."

 :groups
 (
  (group "github-issues" :title "Moved from GitHub issues on 2026-10-14"
   :text "Open GitHub issues of mas-bandwidth/fixed, recorded here on 2026-10-14 when roadmaps became the standard for future work (Glenn, 2026-10-14). Each item names its issue; the issue is closed with a pointer here.")

  (group "export-fixed-perf" :title "Moved out of the sprint on 2026-10-14: fixed-perf"
   :text "The open cards of sprint stream fixed-perf, moved out of the stopped sprint on 2026-10-14, one item per card. Landed cards of the stream are in git and are not listed. Each item names its card, its tier, its base branch and any pushed work to resume from.")
 )

 :items
 (
  (item "issue-29" :group "github-issues" :area "fixed"
   :title "AVX-512 / Zen 5 paths for the hot fixed-point loops: measure scalar vs AVX2 vs AVX-512 on hetzner (Zen 4) and space (Zen 4) first, then vectorize what wins"
   :text "Glenn, 2026-09-22 4:40 PM: 'We can probably make fixed and fixed3d a lot faster by using future cpu instruction sets.' Source: Lemire, 'How did AMD Ryzen get 50% faster in two years' (2026-09-18): 5800X3D -> 9800X3D is 47% faster single-core on Geekbench 6 with only 15% more clock; Zen 5 widened dispatch 6 -> 8, ALUs 4 -> 6, L2 to 1 MB, ROB to 448, and ..."
   :date "2026-10-14"
   :origin "mas-bandwidth/fixed issue #29, opened 2026-09-22 by rowan-claude; moved here 2026-10-14"
   :kept (:github-issue "29" :labels ""))

  (item "fixed-op-bench-harnessb" :group "export-fixed-perf" :area "fixed"
   :title "The fixed op bench: ns per operation, scalar and batch, plus a bit-exact output oracle"
   :text "Every spike in this stream needs a way to measure one operation in nanoseconds and to prove it returns the same bits as before."
   :exists "Work was pushed: head 7d9636f21bb3fbdbe3e1f51bc0b2f6fd4883a6d5 on branch sprint/fixed-op-bench-harnessb.w1.g1.e20 (attempt 1). Resume from it; do not start over."
   :why "Moved out of the stopped sprint on 2026-10-14: neither rocketnet nor v1.3 work."
   :date "2026-10-14"
   :origin "sprint card fixed-op-bench-harnessb (stream fixed-perf, column ready, tier heavy, base sprint/fixed-perf), moved out of the sprint 2026-10-14"
   :kept (:sprint-card "fixed-op-bench-harnessb" :sprint-stream "fixed-perf" :sprint-column "ready" :tier "heavy" :sprint-base "sprint/fixed-perf" :needs "" :pushed-head "7d9636f21bb3fbdbe3e1f51bc0b2f6fd4883a6d5" :pushed-branch "sprint/fixed-op-bench-harnessb.w1.g1.e20"))
  (item "spike-fixmul-product-fits-64-arm" :group "export-fixed-perf" :area "fixed"
   :title "Spike: fixMul takes a one-multiply arm when the product provably fits 63 bits"
   :text "Spike: fixMul takes a one-multiply arm when the product provably fits 63 bits. A tracer-bullet spike: one change, measured end to end once on the perf harness, bit-exact against the scalar oracle, written up with a KEEP or DROP verdict."
   :why "Moved out of the stopped sprint on 2026-10-14: neither rocketnet nor v1.3 work."
   :date "2026-10-14"
   :origin "sprint card spike-fixmul-product-fits-64-arm (stream fixed-perf, column waiting, tier heavy, base sprint/fixed-perf), moved out of the sprint 2026-10-14"
   :kept (:sprint-card "spike-fixmul-product-fits-64-arm" :sprint-stream "fixed-perf" :sprint-column "waiting" :tier "heavy" :sprint-base "sprint/fixed-perf" :needs "fixed-op-bench-harnessb" :pushed-head "" :pushed-branch ""))
  (item "spike-fixdiv-and-int128-division-fast-tiers" :group "export-fixed-perf" :area "fixed"
   :title "Spike: cheaper division, a Newton step with a table seed under an exact correction"
   :text "Spike: cheaper division, a Newton step with a table seed under an exact correction. A tracer-bullet spike: one change, measured end to end once on the perf harness, bit-exact against the scalar oracle, written up with a KEEP or DROP verdict."
   :why "Moved out of the stopped sprint on 2026-10-14: neither rocketnet nor v1.3 work."
   :date "2026-10-14"
   :origin "sprint card spike-fixdiv-and-int128-division-fast-tiers (stream fixed-perf, column waiting, tier heavy, base sprint/fixed-perf), moved out of the sprint 2026-10-14"
   :kept (:sprint-card "spike-fixdiv-and-int128-division-fast-tiers" :sprint-stream "fixed-perf" :sprint-column "waiting" :tier "heavy" :sprint-base "sprint/fixed-perf" :needs "fixed-op-bench-harnessb" :pushed-head "" :pushed-branch ""))
  (item "spike-fixsqrt-seed-and-repair-tiers" :group "export-fixed-perf" :area "fixed"
   :title "Spike: fixSqrt and the 128-bit integer square root, a table seed and fewer repair steps"
   :text "Spike: fixSqrt and the 128-bit integer square root, a table seed and fewer repair steps. A tracer-bullet spike: one change, measured end to end once on the perf harness, bit-exact against the scalar oracle, written up with a KEEP or DROP verdict."
   :why "Moved out of the stopped sprint on 2026-10-14: neither rocketnet nor v1.3 work."
   :date "2026-10-14"
   :origin "sprint card spike-fixsqrt-seed-and-repair-tiers (stream fixed-perf, column waiting, tier heavy, base sprint/fixed-perf), moved out of the sprint 2026-10-14"
   :kept (:sprint-card "spike-fixsqrt-seed-and-repair-tiers" :sprint-stream "fixed-perf" :sprint-column "waiting" :tier "heavy" :sprint-base "sprint/fixed-perf" :needs "fixed-op-bench-harnessb" :pushed-head "" :pushed-branch ""))
  (item "spike-fixnormalize-and-length-kernel" :group "export-fixed-perf" :area "fixed"
   :title "Spike: a cheaper fixNormalize and fixLength, the rsqrt-shaped work (there is no rsqrt in the library)"
   :text "Spike: a cheaper fixNormalize and fixLength, the rsqrt-shaped work (there is no rsqrt in the library). A tracer-bullet spike: one change, measured end to end once on the perf harness, bit-exact against the scalar oracle, written up with a KEEP or DROP verdict."
   :why "Moved out of the stopped sprint on 2026-10-14: neither rocketnet nor v1.3 work."
   :date "2026-10-14"
   :origin "sprint card spike-fixnormalize-and-length-kernel (stream fixed-perf, column waiting, tier heavy, base sprint/fixed-perf), moved out of the sprint 2026-10-14"
   :kept (:sprint-card "spike-fixnormalize-and-length-kernel" :sprint-stream "fixed-perf" :sprint-column "waiting" :tier "heavy" :sprint-base "sprint/fixed-perf" :needs "fixed-op-bench-harnessb" :pushed-head "" :pushed-branch ""))
  (item "spike-trig-evaluation-bit-exact" :group "export-fixed-perf" :area "fixed"
   :title "Spike: faster evaluation of fixComputeCosSin, fixAtan2 and fixLog2/fixExp2 with identical output bits"
   :text "Spike: faster evaluation of fixComputeCosSin, fixAtan2 and fixLog2/fixExp2 with identical output bits. A tracer-bullet spike: one change, measured end to end once on the perf harness, bit-exact against the scalar oracle, written up with a KEEP or DROP verdict."
   :why "Moved out of the stopped sprint on 2026-10-14: neither rocketnet nor v1.3 work."
   :date "2026-10-14"
   :origin "sprint card spike-trig-evaluation-bit-exact (stream fixed-perf, column waiting, tier heavy, base sprint/fixed-perf), moved out of the sprint 2026-10-14"
   :kept (:sprint-card "spike-trig-evaluation-bit-exact" :sprint-stream "fixed-perf" :sprint-column "waiting" :tier "heavy" :sprint-base "sprint/fixed-perf" :needs "fixed-op-bench-harnessb" :pushed-head "" :pushed-branch ""))
  (item "spike-batch-mul-avx2-avx512" :group "export-fixed-perf" :area "fixed"
   :title "Spike: batched fixMul and fixDot over arrays, AVX2 and AVX-512, with runtime dispatch"
   :text "Spike: batched fixMul and fixDot over arrays, AVX2 and AVX-512, with runtime dispatch. A tracer-bullet spike: one change, measured end to end once on the perf harness, bit-exact against the scalar oracle, written up with a KEEP or DROP verdict."
   :why "Moved out of the stopped sprint on 2026-10-14: neither rocketnet nor v1.3 work."
   :date "2026-10-14"
   :origin "sprint card spike-batch-mul-avx2-avx512 (stream fixed-perf, column waiting, tier heavy, base sprint/fixed-perf), moved out of the sprint 2026-10-14"
   :kept (:sprint-card "spike-batch-mul-avx2-avx512" :sprint-stream "fixed-perf" :sprint-column "waiting" :tier "heavy" :sprint-base "sprint/fixed-perf" :needs "fixed-op-bench-harnessb" :pushed-head "" :pushed-branch ""))
  (item "spike-batch-ops-neon" :group "export-fixed-perf" :area "fixed"
   :title "Spike: batched fixed-point operations on NEON, where there is no 64-bit lane multiply"
   :text "Spike: batched fixed-point operations on NEON, where there is no 64-bit lane multiply. A tracer-bullet spike: one change, measured end to end once on the perf harness, bit-exact against the scalar oracle, written up with a KEEP or DROP verdict."
   :why "Moved out of the stopped sprint on 2026-10-14: neither rocketnet nor v1.3 work."
   :date "2026-10-14"
   :origin "sprint card spike-batch-ops-neon (stream fixed-perf, column waiting, tier heavy, base sprint/fixed-perf), moved out of the sprint 2026-10-14"
   :kept (:sprint-card "spike-batch-ops-neon" :sprint-stream "fixed-perf" :sprint-column "waiting" :tier "heavy" :sprint-base "sprint/fixed-perf" :needs "fixed-op-bench-harnessb" :pushed-head "" :pushed-branch ""))
  (item "spike-branchless-saturation" :group "export-fixed-perf" :area "fixed"
   :title "Spike: branchless saturation and overflow handling in the FIX_SATURATE arms"
   :text "Spike: branchless saturation and overflow handling in the FIX_SATURATE arms. A tracer-bullet spike: one change, measured end to end once on the perf harness, bit-exact against the scalar oracle, written up with a KEEP or DROP verdict."
   :why "Moved out of the stopped sprint on 2026-10-14: neither rocketnet nor v1.3 work."
   :date "2026-10-14"
   :origin "sprint card spike-branchless-saturation (stream fixed-perf, column waiting, tier heavy, base sprint/fixed-perf), moved out of the sprint 2026-10-14"
   :kept (:sprint-card "spike-branchless-saturation" :sprint-stream "fixed-perf" :sprint-column "waiting" :tier "heavy" :sprint-base "sprint/fixed-perf" :needs "fixed-op-bench-harnessb" :pushed-head "" :pushed-branch ""))
  (item "spike-int128-intermediate-tricks" :group "export-fixed-perf" :area "fixed"
   :title "Spike: cheaper 128-bit intermediates in the seam (mulx, adc chains, umulh, shift-free rounding)"
   :text "Spike: cheaper 128-bit intermediates in the seam (mulx, adc chains, umulh, shift-free rounding). A tracer-bullet spike: one change, measured end to end once on the perf harness, bit-exact against the scalar oracle, written up with a KEEP or DROP verdict."
   :why "Moved out of the stopped sprint on 2026-10-14: neither rocketnet nor v1.3 work."
   :date "2026-10-14"
   :origin "sprint card spike-int128-intermediate-tricks (stream fixed-perf, column waiting, tier heavy, base sprint/fixed-perf), moved out of the sprint 2026-10-14"
   :kept (:sprint-card "spike-int128-intermediate-tricks" :sprint-stream "fixed-perf" :sprint-column "waiting" :tier "heavy" :sprint-base "sprint/fixed-perf" :needs "fixed-op-bench-harnessb" :pushed-head "" :pushed-branch ""))
  (item "spike-vector-quaternion-kernels-ilp" :group "export-fixed-perf" :area "fixed"
   :title "Spike: instruction-level parallelism in fixRotateVector, fixMulQuat, fixCross and fixMulMV with the same rounding points"
   :text "Spike: instruction-level parallelism in fixRotateVector, fixMulQuat, fixCross and fixMulMV with the same rounding points. A tracer-bullet spike: one change, measured end to end once on the perf harness, bit-exact against the scalar oracle, written up with a KEEP or DROP verdict."
   :why "Moved out of the stopped sprint on 2026-10-14: neither rocketnet nor v1.3 work."
   :date "2026-10-14"
   :origin "sprint card spike-vector-quaternion-kernels-ilp (stream fixed-perf, column waiting, tier heavy, base sprint/fixed-perf), moved out of the sprint 2026-10-14"
   :kept (:sprint-card "spike-vector-quaternion-kernels-ilp" :sprint-stream "fixed-perf" :sprint-column "waiting" :tier "heavy" :sprint-base "sprint/fixed-perf" :needs "fixed-op-bench-harnessb" :pushed-head "" :pushed-branch ""))
))
