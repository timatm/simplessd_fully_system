#ifndef SIMPLESSD_LAYER_PROF_HH
#define SIMPLESSD_LAYER_PROF_HH

#include <algorithm>
#include <cinttypes>
#include <cstdio>
#include <limits>
#include <mutex>
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

/*
 * Breakdown 是給外面拿去聚合用的。
 *
 * incl[]:
 *   原始 inclusive time。
 *   HIL 通常包含 ICL，ICL 包含 FTL，FTL 包含 PAL。
 *
 * excl[]:
 *   exclusive time。
 *   用來算 HIL / ICL / FTL / PAL 占比比較合理。
 *
 * span[]:
 *   該 layer 第一次 begin 到最後一次 end 的 wall-clock span。
 *   這個適合看時間範圍，不適合直接拿來算每層占比。
 */
struct Breakdown {
  bool valid;
  uint64_t id;
  uint64_t slpn;
  uint64_t nlp;

  uint64_t incl[L_COUNT];
  uint64_t excl[L_COUNT];
  uint64_t span[L_COUNT];

  Breakdown() : valid(false), id(0), slpn(0), nlp(0) {
    for (int i = 0; i < L_COUNT; ++i) {
      incl[i] = 0;
      excl[i] = 0;
      span[i] = 0;
    }
  }

  uint64_t inclSum() const {
    uint64_t s = 0;
    for (int i = 0; i < L_COUNT; ++i) {
      s += incl[i];
    }
    return s;
  }

  uint64_t exclSum() const {
    uint64_t s = 0;
    for (int i = 0; i < L_COUNT; ++i) {
      s += excl[i];
    }
    return s;
  }
};

inline std::unordered_map<uint64_t, Rec> &Table() {
  static std::unordered_map<uint64_t, Rec> table;
  return table;
}

inline std::mutex &Mutex() {
  static std::mutex mu;
  return mu;
}

inline uint64_t Sub(uint64_t a, uint64_t b) {
  return a > b ? a - b : 0;
}

inline uint64_t Span(const LayerStat &s) {
  if (s.first == std::numeric_limits<uint64_t>::max()) {
    return 0;
  }

  return s.last - s.first;
}

inline bool ValidLayer(Layer layer) {
  const int idx = static_cast<int>(layer);
  return idx >= 0 && idx < L_COUNT;
}

inline void AddToStat(LayerStat &s, uint64_t begin, uint64_t end) {
  if (end < begin) {
    return;
  }

  s.sum += end - begin;

  if (begin < s.first) {
    s.first = begin;
  }

  if (end > s.last) {
    s.last = end;
  }
}

inline uint64_t NextID() {
  std::lock_guard<std::mutex> lk(Mutex());

  static uint64_t id = 1;
  return id++;
}

inline void BeginReq(uint64_t id, uint64_t slpn, uint64_t nlp) {
  if (id == 0) {
    return;
  }

  std::lock_guard<std::mutex> lk(Mutex());

  Rec rec;
  rec.id = id;
  rec.slpn = slpn;
  rec.nlp = nlp;

  Table()[id] = rec;
}

inline void Start(uint64_t id, Layer layer, uint64_t tick) {
  if (id == 0 || !ValidLayer(layer)) {
    return;
  }

  std::lock_guard<std::mutex> lk(Mutex());

  auto &rec = Table()[id];
  rec.id = id;

  auto &s = rec.layer[layer];
  s.start = tick;
  s.started = true;
}

inline void Add(uint64_t id, Layer layer, uint64_t begin, uint64_t end) {
  if (id == 0 || !ValidLayer(layer) || end < begin) {
    return;
  }

  std::lock_guard<std::mutex> lk(Mutex());

  auto &rec = Table()[id];
  rec.id = id;

  AddToStat(rec.layer[layer], begin, end);
}

inline void End(uint64_t id, Layer layer, uint64_t tick) {
  if (id == 0 || !ValidLayer(layer)) {
    return;
  }

  std::lock_guard<std::mutex> lk(Mutex());

  auto &rec = Table()[id];
  auto &s = rec.layer[layer];

  if (!s.started) {
    return;
  }

  AddToStat(s, s.start, tick);
  s.started = false;
}

inline Breakdown BuildBreakdown(const Rec &r) {
  Breakdown b;

  b.valid = true;
  b.id = r.id;
  b.slpn = r.slpn;
  b.nlp = r.nlp;

  b.incl[L_HIL] = r.layer[L_HIL].sum;
  b.incl[L_ICL] = r.layer[L_ICL].sum;
  b.incl[L_FTL] = r.layer[L_FTL].sum;
  b.incl[L_PAL] = r.layer[L_PAL].sum;

  /*
   * 這裡假設 timing 是 nested：
   *
   * HIL 包含 ICL
   * ICL 包含 FTL
   * FTL 包含 PAL
   *
   * 所以 exclusive time 用扣除方式算。
   */
  b.excl[L_HIL] = Sub(b.incl[L_HIL], b.incl[L_ICL]);
  b.excl[L_ICL] = Sub(b.incl[L_ICL], b.incl[L_FTL]);
  b.excl[L_FTL] = Sub(b.incl[L_FTL], b.incl[L_PAL]);
  b.excl[L_PAL] = b.incl[L_PAL];

  b.span[L_HIL] = Span(r.layer[L_HIL]);
  b.span[L_ICL] = Span(r.layer[L_ICL]);
  b.span[L_FTL] = Span(r.layer[L_FTL]);
  b.span[L_PAL] = Span(r.layer[L_PAL]);

  return b;
}

/*
 * Snapshot 只讀取，不刪除。
 * 通常 debug 才會用到。
 */
inline Breakdown Snapshot(uint64_t id) {
  std::lock_guard<std::mutex> lk(Mutex());

  auto it = Table().find(id);

  if (it == Table().end()) {
    return Breakdown();
  }

  return BuildBreakdown(it->second);
}

/*
 * TakeAndErase 是這次 search profiling 最重要的新 API。
 *
 * 用法：
 *
 *   auto b = SimpleSSD::Prof::TakeAndErase(profId);
 *
 * 之後可以把 b.excl[L_HIL] / b.excl[L_ICL] / b.excl[L_FTL] / b.excl[L_PAL]
 * 加到 SearchKeyState 裡。
 */
inline Breakdown TakeAndErase(uint64_t id) {
  std::lock_guard<std::mutex> lk(Mutex());

  auto it = Table().find(id);

  if (it == Table().end()) {
    return Breakdown();
  }

  Breakdown b = BuildBreakdown(it->second);
  Table().erase(it);

  return b;
}

inline void PrintBreakdown(const Breakdown &b) {
  if (!b.valid) {
    return;
  }

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
      b.id,
      b.slpn,
      b.nlp,
      b.incl[L_HIL],
      b.incl[L_ICL],
      b.incl[L_FTL],
      b.incl[L_PAL],
      b.excl[L_HIL],
      b.excl[L_ICL],
      b.excl[L_FTL],
      b.excl[L_PAL],
      b.span[L_HIL],
      b.span[L_ICL],
      b.span[L_FTL],
      b.span[L_PAL]);
}

inline void PrintAndErase(uint64_t id) {
  Breakdown b = TakeAndErase(id);

  if (!b.valid) {
    return;
  }

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
      b.id,
      b.slpn,
      b.nlp,
      b.incl[L_HIL],
      b.incl[L_ICL],
      b.incl[L_FTL],
      b.incl[L_PAL],
      b.excl[L_HIL],
      b.excl[L_ICL],
      b.excl[L_FTL],
      b.excl[L_PAL],
      b.span[L_HIL],
      b.span[L_ICL],
      b.span[L_FTL],
      b.span[L_PAL]);
}

inline void Clear() {
  std::lock_guard<std::mutex> lk(Mutex());
  Table().clear();
}

}  // namespace Prof
}  // namespace SimpleSSD

#endif