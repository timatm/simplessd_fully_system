#ifndef SIMPLESSD_LAYER_PROF_HH
#define SIMPLESSD_LAYER_PROF_HH

#include <algorithm>
#include <cinttypes>
#include <cstdio>
#include <limits>
#include <unordered_map>

namespace SimpleSSD {
namespace Prof {

enum Layer {
  L_HIL = 0,
  L_ICL = 1,
  L_FTL = 2,
  L_PAL = 3,
  L_COUNT = 4
};

struct LayerStat {
  uint64_t sum = 0;
  uint64_t first = std::numeric_limits<uint64_t>::max();
  uint64_t last = 0;

  uint64_t start = 0;
  bool started = false;
};

struct Rec {
  uint64_t id = 0;
  uint64_t slpn = 0;
  uint64_t nlp = 0;
  LayerStat layer[L_COUNT];
};

inline std::unordered_map<uint64_t, Rec> &Table() {
  static std::unordered_map<uint64_t, Rec> table;
  return table;
}

inline uint64_t NextID() {
  static uint64_t id = 1;
  return id++;
}

inline void BeginReq(uint64_t id, uint64_t slpn, uint64_t nlp) {
  Rec rec;
  rec.id = id;
  rec.slpn = slpn;
  rec.nlp = nlp;
  Table()[id] = rec;
}

inline void Start(uint64_t id, Layer layer, uint64_t tick) {
  if (id == 0) {
    return;
  }

  auto &rec = Table()[id];
  rec.id = id;
  rec.layer[layer].start = tick;
  rec.layer[layer].started = true;
}

inline void Add(uint64_t id, Layer layer, uint64_t begin, uint64_t end) {
  if (id == 0 || end < begin) {
    return;
  }

  auto &rec = Table()[id];
  rec.id = id;

  auto &s = rec.layer[layer];
  s.sum += end - begin;

  if (begin < s.first) {
    s.first = begin;
  }

  if (end > s.last) {
    s.last = end;
  }
}

inline void End(uint64_t id, Layer layer, uint64_t tick) {
  if (id == 0) {
    return;
  }

  auto &rec = Table()[id];
  auto &s = rec.layer[layer];

  if (!s.started) {
    return;
  }

  Add(id, layer, s.start, tick);
  s.started = false;
}

inline uint64_t Span(const LayerStat &s) {
  if (s.first == std::numeric_limits<uint64_t>::max()) {
    return 0;
  }

  return s.last - s.first;
}

inline uint64_t Sub(uint64_t a, uint64_t b) {
  return a > b ? a - b : 0;
}

inline void PrintAndErase(uint64_t id) {
  auto it = Table().find(id);

  if (it == Table().end()) {
    return;
  }

  Rec &r = it->second;

  const uint64_t hil_total = r.layer[L_HIL].sum;
  const uint64_t icl_total = r.layer[L_ICL].sum;
  const uint64_t ftl_total = r.layer[L_FTL].sum;
  const uint64_t pal_total = r.layer[L_PAL].sum;

  const uint64_t hil_excl = Sub(hil_total, icl_total);
  const uint64_t icl_excl = Sub(icl_total, ftl_total);
  const uint64_t ftl_excl = Sub(ftl_total, pal_total);
  const uint64_t pal_excl = pal_total;

  std::fprintf(stderr,
      "FOUR_LAYER req=%" PRIu64
      " slpn=%" PRIu64
      " nlp=%" PRIu64
      " | incl_sum HIL=%" PRIu64
      " ICL=%" PRIu64
      " FTL=%" PRIu64
      " PAL=%" PRIu64
      " | excl_sum HIL=%" PRIu64
      " ICL=%" PRIu64
      " FTL=%" PRIu64
      " PAL=%" PRIu64
      " | span HIL=%" PRIu64
      " ICL=%" PRIu64
      " FTL=%" PRIu64
      " PAL=%" PRIu64
      "\n",
      r.id,
      r.slpn,
      r.nlp,
      hil_total,
      icl_total,
      ftl_total,
      pal_total,
      hil_excl,
      icl_excl,
      ftl_excl,
      pal_excl,
      Span(r.layer[L_HIL]),
      Span(r.layer[L_ICL]),
      Span(r.layer[L_FTL]),
      Span(r.layer[L_PAL]));

  Table().erase(it);
}

}  // namespace Prof
}  // namespace SimpleSSD

#endif