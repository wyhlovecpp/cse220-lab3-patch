/*
 * CSE220 Lab 3 — Victim cache (Jouppi 1990).
 *
 * A tiny fully-associative LRU cache that sits between the L1 dcache and the
 * L2 on the refill path.  Exposes three operations used by dcache_stage.c:
 *
 *   - victim_cache_take(addr, *dirty_out)
 *       Probe the VC for addr.  On hit, REMOVE the entry and return TRUE,
 *       writing its dirty bit through dirty_out.  On miss, return FALSE.
 *       (This matches Jouppi swap-on-hit: the VC hit slot is vacated so the
 *       L1 victim of the same access can reclaim it.)
 *
 *   - victim_cache_install(addr, dirty, *evicted_addr, *evicted_dirty)
 *       Install addr at MRU.  If the VC was full, the LRU entry is evicted
 *       and its (addr, dirty) is written through the out-parameters.  The
 *       caller is responsible for issuing a writeback if evicted_dirty==1.
 *
 *   - victim_cache_init(num_entries)
 *       One-time construction.  Called from init_dcache_stage() when
 *       VICTIM_CACHE_ENABLE != 0.  Re-init resets all state.
 */
#ifndef __VICTIM_CACHE_H__
#define __VICTIM_CACHE_H__

#include "globals/global_types.h"

#ifdef __cplusplus
extern "C" {
#endif

void victim_cache_init(unsigned int proc_id,
                       unsigned int num_entries,
                       unsigned int line_size);

/* Probe-and-remove.  Returns TRUE if addr was resident; in that case the
 * entry is removed from the VC and its dirty bit is returned via dirty_out. */
Flag victim_cache_take(unsigned int proc_id,
                       Addr addr,
                       Flag* dirty_out);

/* Peek: if installing addr would evict an LRU entry, report that entry's
 * addr and dirty bit.  Does NOT modify state.  Caller uses this to check
 * whether a writeback would be required and whether it can be scheduled.
 * Returns TRUE iff the VC is at capacity (so an eviction WOULD occur) and
 * addr is not already resident. */
Flag victim_cache_peek_evict(unsigned int proc_id,
                             Addr addr,
                             Addr* evicted_addr,
                             Flag* evicted_dirty);

/* Install addr at MRU.  If full, the LRU entry is evicted; its address goes
 * in *evicted_addr and its dirty bit in *evicted_dirty.  If no eviction
 * occurred *evicted_addr is set to 0 and *evicted_dirty to FALSE. */
void victim_cache_install(unsigned int proc_id,
                          Addr addr,
                          Flag dirty,
                          Addr* evicted_addr,
                          Flag* evicted_dirty,
                          Flag* did_evict);

#ifdef __cplusplus
}
#endif

#endif
