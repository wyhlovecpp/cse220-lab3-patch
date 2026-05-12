/*
 * CSE220 Lab 3 — Victim cache implementation.
 *
 * Hill/Jouppi-style small fully-associative LRU cache.  Stored here as a
 * std::list of line-aligned {tag, dirty} pairs per proc_id, MRU at front.
 *
 * The caller (dcache_stage.c) does all the L1 / L2 / writeback plumbing;
 * this module only tracks which lines currently live in the VC and the
 * LRU order.
 */
#include "victim_cache.h"

#include "globals/global_defs.h"

#include <cstdint>
#include <list>
#include <unordered_map>
#include <vector>

namespace {

struct Entry {
  uint64_t line_addr;  // real byte address (line-aligned by caller)
  bool dirty;
};

struct ProcVC {
  bool initialized = false;
  unsigned int capacity = 5;
  unsigned int line_shift = 6;
  // MRU at front, LRU at back.  std::list so we can O(1) splice, hash map
  // for O(1) lookup.  (5-entry linear scan would be fine too, but this
  // future-proofs the extra-credit sensitivity sweep up to size 20.)
  std::list<Entry> lru;
  std::unordered_map<uint64_t, std::list<Entry>::iterator> map;
};

std::vector<ProcVC> g_vc;

ProcVC& vc(unsigned int proc_id) {
  if (proc_id >= g_vc.size())
    g_vc.resize(proc_id + 1);
  return g_vc[proc_id];
}

unsigned int ilog2(unsigned int v) {
  unsigned int r = 0;
  while ((1u << r) < v)
    ++r;
  return r;
}

// Line-align to the configured line size.
uint64_t line_of(ProcVC& s, Addr addr) {
  return (static_cast<uint64_t>(addr) >> s.line_shift) << s.line_shift;
}

}  // namespace

extern "C" void victim_cache_init(unsigned int proc_id,
                                  unsigned int num_entries,
                                  unsigned int line_size) {
  ProcVC& s = vc(proc_id);
  s.lru.clear();
  s.map.clear();
  s.capacity = num_entries > 0 ? num_entries : 5;
  s.line_shift = ilog2(line_size > 0 ? line_size : 64);
  s.initialized = true;
}

extern "C" Flag victim_cache_take(unsigned int proc_id,
                                  Addr addr,
                                  Flag* dirty_out) {
  ProcVC& s = vc(proc_id);
  if (!s.initialized) {
    if (dirty_out) *dirty_out = FALSE;
    return FALSE;
  }
  uint64_t key = line_of(s, addr);
  auto it = s.map.find(key);
  if (it == s.map.end()) {
    if (dirty_out) *dirty_out = FALSE;
    return FALSE;
  }
  // Hit: remove the entry and return its dirty bit.
  if (dirty_out) *dirty_out = it->second->dirty ? TRUE : FALSE;
  s.lru.erase(it->second);
  s.map.erase(it);
  return TRUE;
}

extern "C" Flag victim_cache_peek_evict(unsigned int proc_id,
                                        Addr addr,
                                        Addr* evicted_addr,
                                        Flag* evicted_dirty) {
  ProcVC& s = vc(proc_id);
  if (evicted_addr) *evicted_addr = 0;
  if (evicted_dirty) *evicted_dirty = FALSE;
  if (!s.initialized || s.capacity == 0)
    return FALSE;
  uint64_t key = line_of(s, addr);
  if (s.map.find(key) != s.map.end())
    return FALSE;                      // already resident: no eviction
  if (s.lru.size() < s.capacity)
    return FALSE;                      // has room: no eviction
  const Entry& victim = s.lru.back();
  if (evicted_addr) *evicted_addr = victim.line_addr;
  if (evicted_dirty) *evicted_dirty = victim.dirty ? TRUE : FALSE;
  return TRUE;
}

extern "C" void victim_cache_install(unsigned int proc_id,
                                     Addr addr,
                                     Flag dirty,
                                     Addr* evicted_addr,
                                     Flag* evicted_dirty,
                                     Flag* did_evict) {
  ProcVC& s = vc(proc_id);
  if (evicted_addr) *evicted_addr = 0;
  if (evicted_dirty) *evicted_dirty = FALSE;
  if (did_evict) *did_evict = FALSE;
  if (!s.initialized) return;

  uint64_t key = line_of(s, addr);

  // If already resident (should not happen in the normal flow, because the
  // caller probes first), promote and OR dirty; do not double-count a slot.
  auto it = s.map.find(key);
  if (it != s.map.end()) {
    it->second->dirty = it->second->dirty || (dirty ? true : false);
    s.lru.splice(s.lru.begin(), s.lru, it->second);
    return;
  }

  // Evict LRU if full.
  if (s.lru.size() >= s.capacity) {
    Entry& victim = s.lru.back();
    if (evicted_addr) *evicted_addr = victim.line_addr;
    if (evicted_dirty) *evicted_dirty = victim.dirty ? TRUE : FALSE;
    if (did_evict) *did_evict = TRUE;
    s.map.erase(victim.line_addr);
    s.lru.pop_back();
  }

  // Insert at MRU.
  s.lru.push_front(Entry{key, dirty ? true : false});
  s.map[key] = s.lru.begin();
}
