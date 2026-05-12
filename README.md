# CSE220 Lab 3 — Scarab Victim Cache patch

This repo holds the source-code changes used to implement and evaluate a
Jouppi-style victim cache in Scarab for CSE220 Lab 3 (Spring 2026). The
victim cache is a small, fully-associative LRU cache that sits between the
L1 dcache and the L2 on the refill path; see §1 of the lab report PDF for a
summary of Jouppi 1990 (DEC WRL TN-14).

## Files

* `victim_cache.h`, `victim_cache.cc` — new C++ module (with
  `extern "C"` API) implementing the VC. Three entry points:
  - `victim_cache_take(addr, *dirty_out)` — probe-and-remove (swap-on-hit
    semantics: on a hit, the VC slot is vacated so the L1 victim can take
    it).
  - `victim_cache_install(addr, dirty, …)` — MRU-insert, LRU-evict if
    full. Returns the evicted entry's addr + dirty bit.
  - `victim_cache_peek_evict(addr, …)` — non-mutating predicate that tells
    the caller whether a forthcoming install would require a writeback,
    used to guard the deadlock-avoiding writeback-may-fail path.

* `scarab_lab3.patch` — unified diff against Litz-Lab `scarab@main`. Note
  this patch is cumulative on top of the Lab 2 3C-miss-classifier work;
  the Lab 3-specific hunks are labelled `CSE220 Lab3:` and the Lab 2
  hunks are labelled `CSE220 Lab2:`.

* `dcache_stage.c.modified`, `memory.param.def.modified`,
  `memory.stat.def.modified` — full post-patch copies for convenience.

* `PARAMS.kaby_lake_lab3` — the kaby_lake PARAMS file with three stale
  flags stripped (`fetch_across_cache_lines`, `fetch_break_on_taken`,
  `fetch_taken_bubble_cycles`, `extra_recovery_cycles`,
  `extra_redirect_cycles`) and `bp_mech` changed from `tagescl` (no
  longer compiled) to `tage64k`. All other values are untouched.

## Hooks in `dcache_stage.c`

**A. Probe-and-swap on L1 miss.** Inside `update_dcache_stage()`, after
`cache_access` misses and before `dcache_cacheline_miss`:

1. `get_next_repl_line(&dc->dcache, …)` — find which L1 line would be
   evicted to hold the VC-hit line, and snapshot its dirty bit BEFORE any
   `cache_insert` can overwrite the Dcache_Data slot.
2. `victim_cache_take(va, &vc_dirty)` — probe + remove the VC entry for
   `va`. On a miss, fall through to the normal `dcache_cacheline_miss`
   path.
3. On a VC hit the swap is guaranteed not to displace any other VC entry:
   `take()` vacated one slot, and the L1 victim fills exactly that slot.
   So swap-on-hit never issues a writeback.
4. Commit: `cache_insert(&dc->dcache, …)` installs the VC-hit line into
   L1; `victim_cache_install(l1_repl_addr, l1_victim_dirty_snap, …)`
   pushes the L1 victim into the VC with the dirty bit we snapshotted.
   Call `dcache_cacheline_hit` so the op completes as an L1 hit (that
   function increments the read/write counters; we intentionally leave
   them at zero before the call so they are not double-counted).

**B. Install-on-eviction on L1 fill.** Inside `dcache_fill_get_cacheline()`,
when VC is enabled and the fill is an on-path demand fill, skip the
baseline's dirty-writeback-on-eviction branch; instead peek the VC for a
potential dirty-LRU writeback (dead-lock safe the same way), do the L1
`cache_insert`, and then `victim_cache_install` the L1 victim with its
dirty bit captured before the insert overwrites the data slot.

## Parameters

```
DEF_PARAM(victim_cache_enable,  VICTIM_CACHE_ENABLE,  Flag, Flag, FALSE, )
DEF_PARAM(victim_cache_entries, VICTIM_CACHE_ENTRIES, uns,  uns,  5,     )
```

Pass via CLI as `--victim_cache_enable 1 --victim_cache_entries 5`.

## Counters

```
DCACHE_VC_PROBE            — VC probes attempted (= on-path L1 misses probed)
DCACHE_VC_HIT              — on-path L1 misses that hit in the VC
DCACHE_VC_INSTALL          — lines pushed into the VC on L1 eviction
DCACHE_VC_EVICT            — lines LRU-evicted from the VC
DCACHE_VC_EVICT_DIRTY_WB   — VC evictions that issued a writeback to L2
```

## Build

```
cd ~/scarab/src
make opt
```

`victim_cache.cc` is auto-picked up by the existing CMake glob
(`scarab_dirs` in `src/CMakeLists.txt` already includes `.`).

## Sanity / invariants

* VC off: all the Lab 2 invariants still hold; the 3C counters partition
  `DCACHE_MISS_ONPATH` exactly on every run.
* VC on (5 entries): mean miss ratio dropped from 0.280 to 0.268 across
  the 10-benchmark mix. The arithmetic mean of per-benchmark
  `VC_Hit / VC_Probe` is ≈ 0.053 (i.e. the 5-entry VC serves ~5% of the
  on-path L1 misses it sees on average across the 10 benchmarks).
  Benchmarks with substantial locality captured by the VC (perlbench,
  omnetpp, roms) push that per-benchmark hit rate up to 0.08–0.25;
  memory-bound streaming benchmarks (lbm, imagick) sit near zero because
  their miss stream is dominated by never-revisited lines.
* Sensitivity sweep (VC sizes 0/1/2/5/10/20): miss ratio decreases
  monotonically; knee of diminishing returns at ~5 entries, matching
  Jouppi TN-14 §3.1.
