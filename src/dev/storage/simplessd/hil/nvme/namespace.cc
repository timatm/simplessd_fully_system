/*
 * Copyright (C) 2017 CAMELab
 *
 * This file is part of SimpleSSD.
 *
 * SimpleSSD is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * SimpleSSD is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with SimpleSSD.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "hil/nvme/namespace.hh"
#include "ims/include/IMS_interface.hh"
#include "ims/include/persistence.hh"
#include "ims/include/log.hh"
#include "hil/nvme/subsystem.hh"
#include "util/algorithm.hh"
#include "util/layer_prof.hh"


#include "ims/include/lbn_pool.hh"
#include "ims/include/mapping_table.hh"
#include "ims/include/tree.hh"

// extern Tree tree;
// extern LBNPool lbnPoolManager; 
// extern Mapping mappingManager;

#include <algorithm>
#include <cmath>
#include <memory>
#include <functional>
#include <vector>

#include <cstdio>

#define MYDB_LOG(fmt, ...)                                                   \
  do {                                                                       \
    std::fprintf(stderr, "[MYDB-STAT] " fmt "\n", ##__VA_ARGS__);            \
    std::fflush(stderr);                                                     \
  } while (0)
namespace SimpleSSD {

namespace HIL {

namespace NVMe {

struct SearchFlashStats {
  uint64_t total_searches      = 0;
  uint64_t searches_with_flash = 0;
  uint64_t searches_no_flash   = 0;

  uint64_t sum_tflash = 0;
  std::vector<uint64_t> samples;

  void recordNoFlash() {
    total_searches++;
    searches_no_flash++;
  }

  void recordWithFlash(uint64_t tflash) {
    total_searches++;
    searches_with_flash++;
    sum_tflash += tflash;
    samples.push_back(tflash);
  }

  void reset() {
    total_searches = 0;
    searches_with_flash = 0;
    searches_no_flash = 0;
    sum_tflash = 0;
    samples.clear();
  }
};

static SearchFlashStats g_search_flash_stats;

static uint64_t percentileNearestRank(const std::vector<uint64_t> &sorted,
                                      double p) {
  if (sorted.empty()) return 0;
  if (p <= 0.0) return sorted.front();
  if (p >= 1.0) return sorted.back();

  const double rank = std::ceil(p * static_cast<double>(sorted.size()));  // 1..N
  size_t idx = 0;
  if (rank > 1.0) idx = static_cast<size_t>(rank - 1.0);
  if (idx >= sorted.size()) idx = sorted.size() - 1;
  return sorted[idx];
}

static void dumpSearchFlashStatsAndReset() {
  MYDB_LOG(  "[MYDB-STAT] SEARCH_KEY count: total=%" PRIu64
             " with_flash=%" PRIu64 " no_flash=%" PRIu64,
             g_search_flash_stats.total_searches,
             g_search_flash_stats.searches_with_flash,
             g_search_flash_stats.searches_no_flash);

  if (g_search_flash_stats.searches_with_flash == 0) {
    MYDB_LOG("[MYDB-STAT] SEARCH_KEY flash-only ticks: (no samples)");
    g_search_flash_stats.reset();
    return;
  }

  std::vector<uint64_t> sorted = g_search_flash_stats.samples;
  std::sort(sorted.begin(), sorted.end());

  const uint64_t p95 = percentileNearestRank(sorted, 0.95);
  const uint64_t p99 = percentileNearestRank(sorted, 0.99);
  const double avg =
      static_cast<double>(g_search_flash_stats.sum_tflash) /
      static_cast<double>(g_search_flash_stats.searches_with_flash);

  MYDB_LOG(  "[MYDB-STAT] SEARCH_KEY flash-only ticks: avg=%.3f p95=%" PRIu64
             " p99=%" PRIu64 " sum=%" PRIu64,
             avg, p95, p99, g_search_flash_stats.sum_tflash);
  g_search_flash_stats.reset();
}

// Per-command SEARCH_KEY state used to:
//  1) measure flash-only latency T_flash = t_last_done - t_first_issue
//  2) enforce "at most 1 outstanding read per channel" so the simulator behavior
//     matches your rounds metric (max per-channel count).
struct LayerTicks {
    uint64_t hil = 0;
    uint64_t icl = 0;
    uint64_t ftl = 0;
    uint64_t pal = 0;

    void add(const LayerTicks& o) {
        hil += o.hil;
        icl += o.icl;
        ftl += o.ftl;
        pal += o.pal;
    }

    uint64_t sum() const {
        return hil + icl + ftl + pal;
    }
};

struct SearchKeyState {
    IOContext *io = nullptr;

    uint64_t cmd_begin = 0;
    uint64_t cmd_end = 0;

    uint64_t payload_dma_ticks = 0;
    uint64_t ims_decode_ticks = 0;

    uint64_t t_flash_first_issue = 0;
    uint64_t t_flash_last_done   = 0;
    bool     flash_issued        = false;

    size_t total_reads = 0;
    size_t completed   = 0;
    size_t rounds      = 0;

    LayerTicks layer;

    std::vector<std::vector<uint64_t>> per_ch_lpn;
    std::vector<size_t> next_idx;
    std::vector<uint8_t> outstanding;
};

#define DUMP_EACH_SEARCH_AT_CLOSE 1

struct SearchLayerSample {
  uint64_t cmd_ticks = 0;
  uint64_t payload_dma_ticks = 0;
  uint64_t ims_decode_ticks = 0;
  uint64_t flash_wall_ticks = 0;

  uint64_t nreads = 0;
  uint64_t rounds = 0;

  LayerTicks layer;

  bool no_flash = false;
  bool failed = false;
};

struct SearchLayerStats {
  uint64_t total = 0;
  uint64_t no_flash = 0;
  uint64_t failed = 0;
  uint64_t with_flash = 0;

  uint64_t sum_cmd_ticks = 0;
  uint64_t sum_payload_dma_ticks = 0;
  uint64_t sum_decode_ticks = 0;
  uint64_t sum_flash_wall_ticks = 0;

  LayerTicks sum_layer;

  std::vector<SearchLayerSample> samples;

  void reset() {
    total = 0;
    no_flash = 0;
    failed = 0;
    with_flash = 0;

    sum_cmd_ticks = 0;
    sum_payload_dma_ticks = 0;
    sum_decode_ticks = 0;
    sum_flash_wall_ticks = 0;

    sum_layer = LayerTicks{};
    samples.clear();
  }
};

static SearchLayerStats g_search_layer_stats;

static double pct_u64(uint64_t v, uint64_t total) {
  if (total == 0) {
    return 0.0;
  }

  return 100.0 * static_cast<double>(v) / static_cast<double>(total);
}

static void recordSearchLayerSample(const SearchLayerSample &s) {
  g_search_layer_stats.total++;

  if (s.no_flash) {
    g_search_layer_stats.no_flash++;
  }

  if (s.failed) {
    g_search_layer_stats.failed++;
  }

  if (!s.no_flash && !s.failed) {
    g_search_layer_stats.with_flash++;
  }

  g_search_layer_stats.sum_cmd_ticks += s.cmd_ticks;
  g_search_layer_stats.sum_payload_dma_ticks += s.payload_dma_ticks;
  g_search_layer_stats.sum_decode_ticks += s.ims_decode_ticks;
  g_search_layer_stats.sum_flash_wall_ticks += s.flash_wall_ticks;
  g_search_layer_stats.sum_layer.add(s.layer);

  g_search_layer_stats.samples.push_back(s);
}

static void dumpSearchLayerStatsAndReset() {
  const auto &g = g_search_layer_stats;

  MYDB_LOG("SEARCH_LAYER total=%" PRIu64
           " with_flash=%" PRIu64
           " no_flash=%" PRIu64
           " failed=%" PRIu64,
           g.total,
           g.with_flash,
           g.no_flash,
           g.failed);

  if (g.total == 0) {
    MYDB_LOG("SEARCH_LAYER no search samples");
    g_search_layer_stats.reset();
    return;
  }

  MYDB_LOG("SEARCH_LAYER avg_cmd=%.3f avg_payload_dma=%.3f "
           "avg_decode=%.3f avg_flash_wall=%.3f",
           static_cast<double>(g.sum_cmd_ticks) / g.total,
           static_cast<double>(g.sum_payload_dma_ticks) / g.total,
           static_cast<double>(g.sum_decode_ticks) / g.total,
           g.with_flash == 0
               ? 0.0
               : static_cast<double>(g.sum_flash_wall_ticks) / g.with_flash);

  const uint64_t layer_sum = g.sum_layer.sum();

  MYDB_LOG("SEARCH_LAYER sum_excl "
           "HIL=%" PRIu64
           " ICL=%" PRIu64
           " FTL=%" PRIu64
           " PAL=%" PRIu64
           " ratio=%.2f/%.2f/%.2f/%.2f",
           g.sum_layer.hil,
           g.sum_layer.icl,
           g.sum_layer.ftl,
           g.sum_layer.pal,
           pct_u64(g.sum_layer.hil, layer_sum),
           pct_u64(g.sum_layer.icl, layer_sum),
           pct_u64(g.sum_layer.ftl, layer_sum),
           pct_u64(g.sum_layer.pal, layer_sum));

#if DUMP_EACH_SEARCH_AT_CLOSE
  for (size_t i = 0; i < g.samples.size(); ++i) {
    const auto &s = g.samples[i];
    const uint64_t sum = s.layer.sum();

    MYDB_LOG("SEARCH_SAMPLE idx=%zu "
             "cmd=%" PRIu64
             " payload_dma=%" PRIu64
             " decode=%" PRIu64
             " flash_wall=%" PRIu64
             " nreads=%" PRIu64
             " rounds=%" PRIu64
             " no_flash=%d failed=%d "
             "HIL=%" PRIu64
             " ICL=%" PRIu64
             " FTL=%" PRIu64
             " PAL=%" PRIu64
             " ratio=%.2f/%.2f/%.2f/%.2f",
             i,
             s.cmd_ticks,
             s.payload_dma_ticks,
             s.ims_decode_ticks,
             s.flash_wall_ticks,
             s.nreads,
             s.rounds,
             static_cast<int>(s.no_flash),
             static_cast<int>(s.failed),
             s.layer.hil,
             s.layer.icl,
             s.layer.ftl,
             s.layer.pal,
             pct_u64(s.layer.hil, sum),
             pct_u64(s.layer.icl, sum),
             pct_u64(s.layer.ftl, sum),
             pct_u64(s.layer.pal, sum));
  }
#endif

  g_search_layer_stats.reset();
}


Namespace::Namespace(Subsystem *p, ConfigData &c)
    : pParent(p),
      pDisk(nullptr),
      cfgdata(c),
      conf(*c.pConfigReader),
      nsid(NSID_NONE),
      attached(false),
      allocated(false),
      formatFinishedAt(0) {}

// Namespace::~Namespace() {
//   if (pDisk) {
//     delete pDisk;
//   }
// }

Namespace::~Namespace() {
#if RUNTYPE
  ims.close_IMS();
  ims.attachDisk(nullptr);
#endif

  if (pDisk) {
    delete pDisk;
    pDisk = nullptr;
  }
}

void Namespace::submitCommand(SQEntryWrapper &req, RequestFunction &func) {
  CQEntryWrapper resp(req);
  bool response = false;

  if (getTick() < formatFinishedAt) {
    resp.makeStatus(false, false, TYPE_GENERIC_COMMAND_STATUS,
                    STATUS_FORMAT_IN_PROGRESS);

    response = true;
  }
  else {
    // Admin commands
    if (req.sqID == 0) {
      switch (req.entry.dword0.opcode) {
        case OPCODE_GET_LOG_PAGE:
          getLogPage(req, func);
          break;
        default:
          resp.makeStatus(true, false, TYPE_GENERIC_COMMAND_STATUS,
                          STATUS_INVALID_OPCODE);

          response = true;

          break;
      }
    }

    // NVM commands
    else {
      switch (req.entry.dword0.opcode) {
        case OPCODE_FLUSH:
          flush(req, func);
          break;
        case OPCODE_WRITE:
          write(req, func);
          break;
        case OPCODE_READ:
          read(req, func);
          break;
        case OPCODE_COMPARE:
          compare(req, func);
          break;
        case OPCODE_DATASET_MANAGEMEMT:
          datasetManagement(req, func);
          break;
        case OPCODE_SSKEYRANGE:
          debugprint(LOG_IMS,
                     "IMS     | Read SSKeyRnage | SQ %u:%u | CID %u | NSID %-5d",
                     req.sqID, req.sqUID, req.entry.dword0.commandID, nsid);
          read_sskeyrange(req, func);
          break;
        case OPCODE_READ_SSPAGE:
          debugprint(LOG_IMS,
                     "IMS     | Read SStable meta | SQ %u:%u | CID %u | NSID %-5d",
                     req.sqID, req.sqUID, req.entry.dword0.commandID, nsid);
          read_ssPage(req, func);
          break;
        case OPCODE_WRITE_SSTABLE:
          debugprint(LOG_IMS,
                     "IMS     | Write SSTable | SQ %u:%u | CID %u | NSID %-5d",
                     req.sqID, req.sqUID, req.entry.dword0.commandID, nsid);
          write_sstable(req, func);
          break;
        case OPCODE_READ_SSTABLE:
          debugprint(LOG_IMS,
                     "IMS     | Read SSTable  | SQ %u:%u | CID %u | NSID %-5d",
                     req.sqID, req.sqUID, req.entry.dword0.commandID, nsid);
          read_sstable(req, func);
          break;
        case OPCODE_SEARCH_KEY:
          debugprint(LOG_IMS,
                     "IMS     | Search Key     | SQ %u:%u | CID %u | NSID %-5d",
                     req.sqID, req.sqUID, req.entry.dword0.commandID, nsid);
          search_key(req, func);
          break;
        // case OPCODE_IMS_CLOSE:
        //   debugprint(LOG_IMS,
        //              "IMS     | Close IMS      | SQ %u:%u | CID %u | NSID %-5d",
        //              req.sqID, req.sqUID, req.entry.dword0.commandID, nsid);
        //   close_IMS(req, func);
        //   break;
        case OPCODE_COMPACTION_IO:
          debugprint(LOG_IMS,
                     "IMS     | Compaction IO passthru | SQ %u:%u | CID %u | NSID %-5d",
                     req.sqID, req.sqUID, req.entry.dword0.commandID, nsid);
          compaction_io(req, func);
          break;
        case OPCODE_ALLOCATE:
          debugprint(LOG_IMS,
                     "IMS     | Allocate LBN      | SQ %u:%u | CID %u | NSID %-5d",
                     req.sqID, req.sqUID, req.entry.dword0.commandID, nsid);
          allocate_lbn(req, func);
          break;
        case OPCODE_WRITE_LOG:
          debugprint(LOG_IMS,
                     "IMS     | Wriet Log | SQ %u:%u | CID %u | NSID %-5d",
                     req.sqID, req.sqUID, req.entry.dword0.commandID, nsid);
          write_log(req, func);
          break;
        case OPCODE_READ_LOG:
          debugprint(LOG_IMS,
                     "IMS     | Read Log | SQ %u:%u | CID %u | NSID %-5d",
                     req.sqID, req.sqUID, req.entry.dword0.commandID, nsid);
          read_log(req, func);
          break;
        case OPCODE_WRITE_BLOCK:
          debugprint(LOG_IMS,
                     "IMS     | Write Block | SQ %u:%u | CID %u | NSID %-5d",
                     req.sqID, req.sqUID, req.entry.dword0.commandID, nsid);
          write_block(req, func);
          break;
        case OPCODE_READ_BLOCK:
          debugprint(LOG_IMS,
                     "IMS     | Read Block | SQ %u:%u | CID %u | NSID %-5d",
                     req.sqID, req.sqUID, req.entry.dword0.commandID, nsid);
          read_block(req, func);
          break;
        case OPCODE_WRITE_BUFFER:
          debugprint(LOG_IMS,
                     "IMS     | Write Buffer | SQ %u:%u | CID %u | NSID %-5d",
                     req.sqID, req.sqUID, req.entry.dword0.commandID, nsid);
          write_buffer(req, func);
          break;
        case OPCODE_READ_BUFFER:
          debugprint(LOG_IMS,
                     "IMS     | Read Buffer | SQ %u:%u | CID %u | NSID %-5d",
                     req.sqID, req.sqUID, req.entry.dword0.commandID, nsid);
          read_buffer(req, func);
          break;
        case OPCODE_OPEN_DB:
          debugprint(LOG_IMS,
                     "IMS     | Open DB | SQ %u:%u | CID %u | NSID %-5d",
                     req.sqID, req.sqUID, req.entry.dword0.commandID, nsid);
          open_DB(req, func);
          break;
        case OPCODE_CLOSE_DB:
          debugprint(LOG_IMS,
                     "IMS     | Close DB | SQ %u:%u | CID %u | NSID %-5d",
                     req.sqID, req.sqUID, req.entry.dword0.commandID, nsid);
          close_DB(req, func);
          break;
        case OPCODE_ERASE_SSTABLE:
          debugprint(LOG_IMS,
                     "IMS     | Erase SStable | SQ %u:%u | CID %u | NSID %-5d",
                     req.sqID, req.sqUID, req.entry.dword0.commandID, nsid);
          erase_sstable(req, func);
          break;
        case OPCODE_TRIVIL_MOVE:
          debugprint(LOG_IMS,
                     "IMS     | Trival Move | SQ %u:%u | CID %u | NSID %-5d",
                     req.sqID, req.sqUID, req.entry.dword0.commandID, nsid);
          trival_move(req, func);
          break;
        default:
          debugprint(
            LOG_IMS,
            "IMS     | Unknown opcode %02X | SQ %u:%u | CID %u | NSID %-5d",
            req.entry.dword0.opcode, req.sqID, req.sqUID,
            req.entry.dword0.commandID, nsid);
          resp.makeStatus(true, false, TYPE_GENERIC_COMMAND_STATUS,
                          STATUS_INVALID_OPCODE);

          response = true;

          break;
      }
    }
  }

  if (response) {
    func(resp);
  }
}

void Namespace::setData(uint32_t id, Information *data) {
  nsid = id;
  memcpy(&info, data, sizeof(Information));

  if (conf.readBoolean(CONFIG_NVME, NVME_ENABLE_DISK_IMAGE)) {
    uint64_t diskSize;

    std::string filename =
        conf.readString(CONFIG_NVME, NVME_DISK_IMAGE_PATH + nsid);

    if (filename.length() == 0) {
      pDisk = new MemDisk();
    }
    else if (conf.readBoolean(CONFIG_NVME, NVME_USE_COW_DISK)) {
      pDisk = new CoWDisk();
    }
    else {
      pDisk = new Disk();
    }

    diskSize = pDisk->open(filename, info.size * info.lbaSize, info.lbaSize);

    if (diskSize == 0) {
      panic("Failed to open disk image");
    }
    else if (diskSize != info.size * info.lbaSize) {
      if (conf.readBoolean(CONFIG_NVME, NVME_STRICT_DISK_SIZE)) {
        panic("Disk size not match");
      }
    }

    if (filename.length() > 0) {
      SimpleSSD::info("Using disk image at %s for NSID %u", filename.c_str(),
                      nsid);
    }
  }
  #if RUNTYPE
    ims.attachDisk(pDisk);
  #endif
  ims.init_IMS();
  allocated = true;
}

void Namespace::attach(bool attach) {
  attached = attach;
}

uint32_t Namespace::getNSID() {
  return nsid;
}

Namespace::Information *Namespace::getInfo() {
  return &info;
}

bool Namespace::isAttached() {
  return attached;
}

void Namespace::format(uint64_t tick) {
  formatFinishedAt = tick;

  health = HealthInfo();

  if (pDisk) {
    delete pDisk;
    pDisk = nullptr;
  }
}

void Namespace::getLogPage(SQEntryWrapper &req, RequestFunction &func) {
  CQEntryWrapper resp(req);
  uint16_t numdl = (req.entry.dword10 & 0xFFFF0000) >> 16;
  uint16_t lid = req.entry.dword10 & 0xFFFF;
  uint16_t numdu = req.entry.dword11 & 0xFFFF;
  uint32_t lopl = req.entry.dword12;
  uint32_t lopu = req.entry.dword13;
  bool submit = true;

  uint32_t req_size = (((uint32_t)numdu << 16 | numdl) + 1) * 4;
  uint64_t offset = ((uint64_t)lopu << 32) | lopl;

  debugprint(LOG_HIL_NVME,
             "ADMIN   | Get Log Page | Log %d | Size %d | NSID %d", lid,
             req_size, nsid);

  static DMAFunction dmaDone = [](uint64_t, void *context) {
    RequestContext *pContext = (RequestContext *)context;

    pContext->function(pContext->resp);

    delete pContext->dma;
    delete pContext;
  };
  DMAFunction smartInfo = [offset](uint64_t, void *context) {
    RequestContext *pContext = (RequestContext *)context;

    pContext->dma->write(offset, 512, pContext->buffer, dmaDone, context);
  };

  switch (lid) {
    case LOG_SMART_HEALTH_INFORMATION:
      if (req.entry.namespaceID == nsid) {
        submit = false;
        RequestContext *pContext = new RequestContext(func, resp);

        pContext->buffer = health.data;

        if (req.useSGL) {
          pContext->dma = new SGL(cfgdata, smartInfo, pContext, req.entry.data1,
                                  req.entry.data2);
        }
        else {
          pContext->dma =
              new PRPList(cfgdata, smartInfo, pContext, req.entry.data1,
                          req.entry.data2, (uint64_t)req_size);
        }
      }
      else {
        resp.makeStatus(true, false, TYPE_COMMAND_SPECIFIC_STATUS,
                        STATUS_NAMESPACE_NOT_ATTACHED);
      }

      break;
    default:
      resp.makeStatus(true, false, TYPE_COMMAND_SPECIFIC_STATUS,
                      STATUS_INVALID_LOG_PAGE);
      break;
  }

  if (submit) {
    func(resp);
  }
}

void Namespace::flush(SQEntryWrapper &req, RequestFunction &func) {
  bool err = false;

  CQEntryWrapper resp(req);

  if (!attached) {
    err = true;
    resp.makeStatus(true, false, TYPE_COMMAND_SPECIFIC_STATUS,
                    STATUS_NAMESPACE_NOT_ATTACHED);

    func(resp);
  }

  if (!err) {
    DMAFunction begin = [this](uint64_t, void *context) {
      DMAFunction doFlush = [this](uint64_t now, void *context) {
        IOContext *pContext = (IOContext *)context;

        debugprint(
            LOG_HIL_NVME,
            "NVM     | FLUSH | CQ %u | SQ %u:%u | CID %u | NSID %-5d | %" PRIu64
            " - %" PRIu64 " (%" PRIu64 ")",
            pContext->resp.cqID, pContext->resp.entry.dword2.sqID,
            pContext->resp.sqUID, pContext->resp.entry.dword3.commandID, nsid,
            pContext->beginAt, now, now - pContext->beginAt);

        pContext->function(pContext->resp);

        delete pContext;
      };

      pParent->flush(this, doFlush, context);
    };

    debugprint(LOG_HIL_NVME, "NVM     | FLUSH | SQ %u:%u | CID %u |  NSID %-5d",
               req.sqID, req.sqUID, req.entry.dword0.commandID, nsid);

    IOContext *pContext = new IOContext(func, resp);

    pContext->beginAt = getTick();

    execute(CPU::NVME__NAMESPACE, CPU::FLUSH, begin, pContext);
  }
}

void Namespace::write(SQEntryWrapper &req, RequestFunction &func) {
  bool err = false;

  CQEntryWrapper resp(req);
  uint64_t slba = ((uint64_t)req.entry.dword11 << 32) | req.entry.dword10;
  uint16_t nlb = (req.entry.dword12 & 0xFFFF) + 1;

  if (!attached) {
    err = true;
    resp.makeStatus(true, false, TYPE_COMMAND_SPECIFIC_STATUS,
                    STATUS_NAMESPACE_NOT_ATTACHED);
  }
  if (nlb == 0) {
    err = true;
    warn("nvme_namespace: host tried to write 0 blocks");
  }

  debugprint(LOG_HIL_NVME,
             "NVM     | WRITE | SQ %u:%u | CID %u | NSID %-5d | %" PRIX64
             " + %d",
             req.sqID, req.sqUID, req.entry.dword0.commandID, nsid, slba, nlb);

  if (!err) {
    DMAFunction doRead = [this](uint64_t tick, void *context) {
      DMAFunction dmaDone = [this](uint64_t tick, void *context) {
        IOContext *pContext = (IOContext *)context;

        pContext->beginAt++;

        if (pContext->beginAt == 2) {
          debugprint(
              LOG_HIL_NVME,
              "NVM     | WRITE | CQ %u | SQ %u:%u | CID %u | NSID %-5d | "
              "%" PRIX64 " + %d | %" PRIu64 " - %" PRIu64 " (%" PRIu64 ")",
              pContext->resp.cqID, pContext->resp.entry.dword2.sqID,
              pContext->resp.sqUID, pContext->resp.entry.dword3.commandID, nsid,
              pContext->slba, pContext->nlb, pContext->tick, tick,
              tick - pContext->tick);
          pContext->function(pContext->resp);

          if (pContext->buffer) {
            pDisk->write(pContext->slba, pContext->nlb, pContext->buffer);

            free(pContext->buffer);
          }

          delete pContext->dma;
          delete pContext;
        }
      };

      IOContext *pContext = (IOContext *)context;

      pContext->tick = tick;
      pContext->beginAt = 0;

      if (pDisk) {
        pContext->buffer = (uint8_t *)calloc(pContext->nlb, info.lbaSize);

        pContext->dma->read(0, pContext->nlb * info.lbaSize, pContext->buffer,
                            dmaDone, context);
      }
      else {
        pContext->dma->read(0, pContext->nlb * info.lbaSize, nullptr, dmaDone,
                            context);
      }

      pParent->write(this, pContext->slba, pContext->nlb, dmaDone, context);
    };

    IOContext *pContext = new IOContext(func, resp);

    pContext->beginAt = getTick();
    pContext->slba = slba;
    pContext->nlb = nlb;

    CPUContext *pCPU =
        new CPUContext(doRead, pContext, CPU::NVME__NAMESPACE, CPU::WRITE);

    if (req.useSGL) {
      pContext->dma =
          new SGL(cfgdata, cpuHandler, pCPU, req.entry.data1, req.entry.data2);
    }
    else {
      pContext->dma =
          new PRPList(cfgdata, cpuHandler, pCPU, req.entry.data1,
                      req.entry.data2, (uint64_t)nlb * info.lbaSize);
    }
  }
  else {
    func(resp);
  }
}

void Namespace::read(SQEntryWrapper &req, RequestFunction &func) {
  bool err = false;

  CQEntryWrapper resp(req);
  uint64_t slba = ((uint64_t)req.entry.dword11 << 32) | req.entry.dword10;
  uint16_t nlb = (req.entry.dword12 & 0xFFFF) + 1;
  // bool fua = req.entry.dword12 & 0x40000000;

  if (!attached) {
    err = true;
    resp.makeStatus(true, false, TYPE_COMMAND_SPECIFIC_STATUS,
                    STATUS_NAMESPACE_NOT_ATTACHED);
  }
  if (nlb == 0) {
    err = true;
    warn("nvme_namespace: host tried to read 0 blocks");
  }

  debugprint(LOG_HIL_NVME,
             "NVM     | READ  | SQ %u:%u | CID %u | NSID %-5d | %" PRIX64
             " + %d",
             req.sqID, req.sqUID, req.entry.dword0.commandID, nsid, slba, nlb);

  if (!err) {
    DMAFunction doRead = [this](uint64_t tick, void *context) {
      DMAFunction dmaDone = [this](uint64_t tick, void *context) {
        IOContext *pContext = (IOContext *)context;

        pContext->beginAt++;

        if (pContext->beginAt == 2) {
          debugprint(
              LOG_HIL_NVME,
              "NVM     | READ  | CQ %u | SQ %u:%u | CID %u | NSID %-5d | "
              "%" PRIX64 " + %d | %" PRIu64 " - %" PRIu64 " (%" PRIu64 ")",
              pContext->resp.cqID, pContext->resp.entry.dword2.sqID,
              pContext->resp.sqUID, pContext->resp.entry.dword3.commandID, nsid,
              pContext->slba, pContext->nlb, pContext->tick, tick,
              tick - pContext->tick);

          pContext->function(pContext->resp);

          if (pContext->buffer) {
            free(pContext->buffer);
          }

          delete pContext->dma;
          delete pContext;
        }
      };

      IOContext *pContext = (IOContext *)context;

      pContext->tick = tick;
      pContext->beginAt = 0;

      pParent->read(this, pContext->slba, pContext->nlb, dmaDone, pContext);

      pContext->buffer = (uint8_t *)calloc(pContext->nlb, info.lbaSize);

      if (pDisk) {
        pDisk->read(pContext->slba, pContext->nlb, pContext->buffer);
      }

      pContext->dma->write(0, pContext->nlb * info.lbaSize, pContext->buffer,
                           dmaDone, context);
    };

    IOContext *pContext = new IOContext(func, resp);

    pContext->beginAt = getTick();
    pContext->slba = slba;
    pContext->nlb = nlb;

    CPUContext *pCPU =
        new CPUContext(doRead, pContext, CPU::NVME__NAMESPACE, CPU::READ);

    if (req.useSGL) {
      pContext->dma =
          new SGL(cfgdata, cpuHandler, pCPU, req.entry.data1, req.entry.data2);
    }
    else {
      pContext->dma =
          new PRPList(cfgdata, cpuHandler, pCPU, req.entry.data1,
                      req.entry.data2, pContext->nlb * info.lbaSize);
    }
  }
  else {
    func(resp);
  }
}

void Namespace::compare(SQEntryWrapper &req, RequestFunction &func) {
  bool err = false;

  CQEntryWrapper resp(req);
  uint64_t slba = ((uint64_t)req.entry.dword11 << 32) | req.entry.dword10;
  uint16_t nlb = (req.entry.dword12 & 0xFFFF) + 1;
  // bool fua = req.entry.dword12 & 0x40000000;log_store

  if (!attached) {
    err = true;
    resp.makeStatus(true, false, TYPE_COMMAND_SPECIFIC_STATUS,
                    STATUS_NAMESPACE_NOT_ATTACHED);
  }
  if (nlb == 0) {
    err = true;
    warn("nvme_namespace: host tried to read 0 blocks");
  }

  debugprint(LOG_HIL_NVME,
             "NVM     | COMP  | SQ %u:%u | CID %u | NSID %-5d | %" PRIX64
             " + %d",
             req.sqID, req.sqUID, req.entry.dword0.commandID, nsid, slba, nlb);

  if (!err) {
    DMAFunction doRead = [this](uint64_t tick, void *context) {
      DMAFunction dmaDone = [this](uint64_t tick, void *context) {
        CompareContext *pContext = (CompareContext *)context;

        pContext->beginAt++;

        if (pContext->beginAt == 2) {
          // Compare buffer!
          // Always success if no disk
          if (pDisk && memcmp(pContext->buffer, pContext->hostContent,
                              pContext->nlb * info.lbaSize) != 0) {
            pContext->resp.makeStatus(false, false,
                                      TYPE_MEDIA_AND_DATA_INTEGRITY_ERROR,
                                      STATUS_COMPARE_FAILURE);
          }

          debugprint(
              LOG_HIL_NVME,
              "NVM     | COMP  | CQ %u | SQ %u:%u | CID %u | NSID %-5d | "
              "%" PRIX64 " + %d | %" PRIu64 " - %" PRIu64 " (%" PRIu64 ")",
              pContext->resp.cqID, pContext->resp.entry.dword2.sqID,
              pContext->resp.sqUID, pContext->resp.entry.dword3.commandID, nsid,
              pContext->slba, pContext->nlb, pContext->tick, tick,
              tick - pContext->tick);

          pContext->function(pContext->resp);

          if (pContext->buffer) {
            free(pContext->buffer);
          }
          if (pContext->hostContent) {
            free(pContext->hostContent);
          }

          delete pContext->dma;
          delete pContext;
        }
      };

      CompareContext *pContext = (CompareContext *)context;

      pContext->tick = tick;
      pContext->beginAt = 0;

      pParent->read(this, pContext->slba, pContext->nlb, dmaDone, pContext);

      pContext->buffer = (uint8_t *)calloc(pContext->nlb, info.lbaSize);
      pContext->hostContent = (uint8_t *)calloc(pContext->nlb, info.lbaSize);

      if (pDisk) {
        pDisk->read(pContext->slba, pContext->nlb, pContext->buffer);
      }

      pContext->dma->read(0, pContext->nlb * info.lbaSize,
                          pContext->hostContent, dmaDone, context);
    };

    CompareContext *pContext = new CompareContext(func, resp);

    pContext->beginAt = getTick();
    pContext->slba = slba;
    pContext->nlb = nlb;

    CPUContext *pCPU =
        new CPUContext(doRead, pContext, CPU::NVME__NAMESPACE, CPU::READ);

    if (req.useSGL) {
      pContext->dma =
          new SGL(cfgdata, cpuHandler, pCPU, req.entry.data1, req.entry.data2);
    }
    else {
      pContext->dma =
          new PRPList(cfgdata, cpuHandler, pCPU, req.entry.data1,
                      req.entry.data2, pContext->nlb * info.lbaSize);
    }
  }
  else {
    func(resp);
  }
}

void Namespace::datasetManagement(SQEntryWrapper &req, RequestFunction &func) {
  bool err = false;

  CQEntryWrapper resp(req);
  int nr = (req.entry.dword10 & 0xFF) + 1;
  bool ad = req.entry.dword11 & 0x04;

  if (!attached) {
    err = true;
    resp.makeStatus(true, false, TYPE_COMMAND_SPECIFIC_STATUS,
                    STATUS_NAMESPACE_NOT_ATTACHED);
  }
  if (!ad) {
    err = true;
    // Just ignore
  }

  debugprint(
      LOG_HIL_NVME,
      "NVM     | TRIM  | SQ %u:%u | CID %u |  NSID %-5d| %d ranges | Attr %1X",
      req.sqID, req.sqUID, req.entry.dword0.commandID, nsid, nr,
      req.entry.dword11 & 0x0F);

  if (!err) {
    static DMAFunction eachTrimDone = [](uint64_t tick, void *context) {
      DMAContext *pContext = (DMAContext *)context;

      pContext->counter--;

      if (pContext->counter == 0) {
        pContext->function(tick, pContext->context);
      }

      delete pContext;
    };
    DMAFunction doTrim = [this](uint64_t, void *context) {
      DMAFunction dmaDone = [this](uint64_t, void *context) {
        DMAFunction trimDone = [this](uint64_t tick, void *context) {
          IOContext *pContext = (IOContext *)context;

          debugprint(LOG_HIL_NVME,
                     "NVM     | TRIM  | CQ %u | SQ %u:%u | CID %u | NSID %-5d| "
                     "%" PRIu64 " - %" PRIu64 " (%" PRIu64 ")",
                     pContext->resp.cqID, pContext->resp.entry.dword2.sqID,
                     pContext->resp.sqUID,
                     pContext->resp.entry.dword3.commandID, nsid,
                     pContext->beginAt, tick, tick - pContext->beginAt);

          pContext->function(pContext->resp);

          delete pContext;
        };

        DatasetManagementRange range;
        IOContext *pContext = (IOContext *)context;
        DMAContext *pDMA = new DMAContext(trimDone);

        pDMA->context = context;

        for (uint64_t i = 0; i < pContext->slba; i++) {
          memcpy(range.data,
                 pContext->buffer + i * sizeof(DatasetManagementRange),
                 sizeof(DatasetManagementRange));

          pDMA->counter++;
          pParent->trim(this, range.slba, range.nlb, eachTrimDone, pDMA);
        }

        if (pDMA->counter == 0) {
          pDMA->counter = 1;

          eachTrimDone(getTick(), pDMA);
        }

        free(pContext->buffer);
        delete pContext->dma;
      };

      IOContext *pContext = (IOContext *)context;

      pContext->buffer =
          (uint8_t *)calloc(pContext->slba, sizeof(DatasetManagementRange));

      pContext->dma->read(0, pContext->slba * sizeof(DatasetManagementRange),
                          pContext->buffer, dmaDone, context);
    };

    IOContext *pContext = new IOContext(func, resp);

    pContext->beginAt = getTick();
    pContext->slba = nr;

    CPUContext *pCPU = new CPUContext(doTrim, pContext, CPU::NVME__NAMESPACE,
                                      CPU::DATASET_MANAGEMENT);

    if (req.useSGL) {
      pContext->dma =
          new SGL(cfgdata, cpuHandler, pCPU, req.entry.data1, req.entry.data2);
    }
    else {
      pContext->dma = new PRPList(cfgdata, cpuHandler, pCPU, req.entry.data1,
                                  req.entry.data2, (uint64_t)nr * 0x10);
    }
  }
  else {
    func(resp);
  }
}

// Custom command implementation

void Namespace::write_sstable(SQEntryWrapper &req, RequestFunction &func) {
  bool err = false;

  CQEntryWrapper resp(req);  
  // uint64_t slba = ((uint64_t)req.entry.dword11 << 32) | req.entry.dword10;
  // uint16_t nlb = (req.entry.dword12 & 0xFFFF) + 1;
  uint32_t isCompaction = req.entry.dword12;

  if (!attached) {
    err = true;
    resp.makeStatus(true, false, TYPE_COMMAND_SPECIFIC_STATUS,
                    STATUS_NAMESPACE_NOT_ATTACHED);
  }
  uint64_t lbn = INVALIDLBN;
  if(isCompaction){
    err = ims.write_sstable(lbn,true);
  }
  else{
    err = ims.write_sstable(lbn,false);
  }
  
  if(lbn == INVALIDLBN){
    err = true;
    debugprint(LOG_IMS,
             "NVM     | WRITE_SSTABLE | Command failed");
    resp.makeStatus(true, false, TYPE_COMMAND_SPECIFIC_STATUS,
                    STATUS_COMMAND_FAILD);
  }
  // if (nlb == 0) {
  //   err = true;
  //   warn("nvme_namespace: host tried to write 0 blocks");
  // }
  
  if (!err) {
    DMAFunction doread = [this](uint64_t tick, void *context) {
      DMAFunction dmaDone = [this](uint64_t tick, void *context) {
        IOContext *pContext = (IOContext *)context;

        pContext->beginAt++;

        if (pContext->beginAt == 2) {
          debugprint(
            LOG_IMS,
            "NVM     | WRITE_SSTABLE | CQ %u | SQ %u:%u | CID %u | NSID %-5d | "
            "LBN %ld | LPN %ld + %ld | %" PRIu64 " - %" PRIu64 " (%" PRIu64 ")",
            pContext->resp.cqID,
            pContext->resp.entry.dword2.sqID,
            pContext->resp.sqUID,
            pContext->resp.entry.dword3.commandID,
            nsid,
            pContext->lbn,
            pContext->lpn,
            pContext->nlpn,
            pContext->tick,
            tick,
            tick - pContext->tick);

          pContext->function(pContext->resp);

          if (pContext->buffer) {
            pDisk->writeBlock(pContext->lbn,pContext->buffer);

            free(pContext->buffer);
          }

          delete pContext->dma;
          delete pContext;
        }
      };

      IOContext *pContext = (IOContext *)context;

      pContext->tick = tick;
      pContext->beginAt = 0;

      if (pDisk) {
        pContext->buffer = (uint8_t *)calloc(BLOCK_SIZE, 1);

        pContext->dma->read(0, (uint64_t)BLOCK_SIZE, pContext->buffer,
                            dmaDone, context);
      }
      else {
        pContext->dma->read(0, (uint64_t)BLOCK_SIZE, nullptr, dmaDone,
                            context);
      }

      pParent->writeIMS(this, pContext->lpn, pContext->nlpn, dmaDone, context);
    };

    IOContext *pContext = new IOContext(func, resp);

    pContext->beginAt = getTick();
    pContext->lpn = LBN2LPN(lbn);
    pContext->nlpn = IMS_PAGE_NUM;
    pContext->lbn = lbn;
    debugprint(LOG_IMS,
              "NVM     | WRITE_SSTABLE | IOContext | LPN: %ld (LBN: %ld)| number of LPN: %ld",pContext->lpn ,lbn,pContext->nlpn);

    CPUContext *pCPU =
        new CPUContext(doread, pContext, CPU::NVME__NAMESPACE, CPU::WRITE);

    if (req.useSGL) {
      pContext->dma =
          new SGL(cfgdata, cpuHandler, pCPU, req.entry.data1, req.entry.data2);
    }
    else {
      pContext->dma =
          new PRPList(cfgdata, cpuHandler, pCPU, req.entry.data1,
                      req.entry.data2, (uint64_t)BLOCK_SIZE);
    }
  }
  else {
    func(resp);
  }
}

void Namespace::write_log(SQEntryWrapper &req, RequestFunction &func) {
  bool err = false;

  CQEntryWrapper resp(req);

  // Parse IMS command
  
  uint64_t lpn = ((uint64_t)req.entry.reserved2) << 32 | req.entry.reserved1;

  // uint8_t *buffer  = new uint8_t[2]; // dummy buffer not real data
  // err = (bool)ims.write_log(lpn,buffer);
  
  // uint64_t slba = ((uint64_t)req.entry.dword11 << 32) | req.entry.dword10;
  // uint16_t nlb = (req.entry.dword12 & 0xFFFF) + 1;

  if (!attached) {
    err = true;
    resp.makeStatus(true, false, TYPE_COMMAND_SPECIFIC_STATUS,
                    STATUS_NAMESPACE_NOT_ATTACHED);
  }
  if(err){
    debugprint(LOG_IMS,
             "NVM     | WRITE LOG | Command failed");
    resp.makeStatus(true, false, TYPE_COMMAND_SPECIFIC_STATUS,
                    STATUS_COMMAND_FAILD);
  }
  debugprint(LOG_IMS,
             "NVM     | WRITE LOG | LBN: %lu | LPN: %lu",
             LPN2LBN(lpn),lpn);
  if (!err) {
    DMAFunction doread = [this](uint64_t tick, void *context) {
      DMAFunction dmaDone = [this](uint64_t tick, void *context) {
        IOContext *pContext = (IOContext *)context;

        pContext->beginAt++;

        if (pContext->beginAt == 2) {
          debugprint(
              LOG_IMS,
              "NVM     | WRITE LOG | CQ %u | SQ %u:%u | CID %u | NSID %-5d | "
              "%" PRIX64 " + %d | %" PRIu64 " - %" PRIu64 " (%" PRIu64 ")",
              pContext->resp.cqID, pContext->resp.entry.dword2.sqID,
              pContext->resp.sqUID, pContext->resp.entry.dword3.commandID, nsid,
              pContext->slba, pContext->nlb, pContext->tick, tick,
              tick - pContext->tick);
          pContext->function(pContext->resp);

          if (pContext->buffer) {
            pDisk->writePage(pContext->lpn,pContext->buffer);

            free(pContext->buffer);
          }

          delete pContext->dma;
          delete pContext;
        }
      };

      IOContext *pContext = (IOContext *)context;

      pContext->tick = tick;
      pContext->beginAt = 0;

      if (pDisk) {
        pContext->buffer = (uint8_t *)calloc(IMS_PAGE_SIZE, 1);

        pContext->dma->read(0, (uint64_t)IMS_PAGE_SIZE, pContext->buffer,
                            dmaDone, context);
      }
      else {
        pContext->dma->read(0, (uint64_t)IMS_PAGE_SIZE, nullptr, dmaDone,
                            context);
      }

      pParent->writeIMS(this, pContext->lpn, pContext->nlpn, dmaDone, context);
    };

    IOContext *pContext = new IOContext(func, resp);

    pContext->beginAt = getTick();
    pContext->lpn = lpn;
    pContext->nlpn = 1;
    pContext->lbn = LPN2LBN(lpn);
    debugprint(LOG_IMS,
              "NVM     | WRITE LOG  | IOContext | LPN: %ld (LBN: %ld)| number of LPN: %ld",pContext->lpn ,pContext->lbn,pContext->nlpn);

    CPUContext *pCPU =
        new CPUContext(doread, pContext, CPU::NVME__NAMESPACE, CPU::WRITE);

    if (req.useSGL) {
      pContext->dma =
          new SGL(cfgdata, cpuHandler, pCPU, req.entry.data1, req.entry.data2);
    }
    else {
      pContext->dma =
          new PRPList(cfgdata, cpuHandler, pCPU, req.entry.data1,
                      req.entry.data2, (uint64_t)IMS_PAGE_SIZE);
    }
  }
  else {
    func(resp);
  }
}

void Namespace::write_block(SQEntryWrapper &req, RequestFunction &func) {
  bool err = false;

  CQEntryWrapper resp(req);

  // Parse IMS command
  
  uint32_t lbn = req.entry.dword12;

  // uint8_t *buffer  = new uint8_t[2]; // dummy buffer not real data
  // err = (bool)ims.write_log(lpn,buffer);
  
  // uint64_t slba = ((uint64_t)req.entry.dword11 << 32) | req.entry.dword10;
  // uint16_t nlb = (req.entry.dword12 & 0xFFFF) + 1;

  if (!attached) {
    err = true;
    resp.makeStatus(true, false, TYPE_COMMAND_SPECIFIC_STATUS,
                    STATUS_NAMESPACE_NOT_ATTACHED);
  }
  if(err){
    debugprint(LOG_IMS,
             "NVM     | WRITE BLOCK | Command failed");
    resp.makeStatus(true, false, TYPE_COMMAND_SPECIFIC_STATUS,
                    STATUS_COMMAND_FAILD);
  }
  debugprint(LOG_IMS,
             "NVM     | WRITE BLOCK | LBN: %lu ",
             lbn);
  if (!err) {
    DMAFunction doread = [this](uint64_t tick, void *context) {
      DMAFunction dmaDone = [this](uint64_t tick, void *context) {
        IOContext *pContext = (IOContext *)context;

        pContext->beginAt++;

        if (pContext->beginAt == 2) {
          debugprint(
              LOG_IMS,
              "NVM     | WRITE BLOCK | CQ %u | SQ %u:%u | CID %u | NSID %-5d | "
              "%" PRIX64 " + %d | %" PRIu64 " - %" PRIu64 " (%" PRIu64 ")",
              pContext->resp.cqID, pContext->resp.entry.dword2.sqID,
              pContext->resp.sqUID, pContext->resp.entry.dword3.commandID, nsid,
              pContext->slba, pContext->nlb, pContext->tick, tick,
              tick - pContext->tick);
          pContext->function(pContext->resp);

          if (pContext->buffer) {
            pDisk->writeBlock(pContext->lbn,pContext->buffer);
            free(pContext->buffer);
          }

          delete pContext->dma;
          delete pContext;
        }
      };

      IOContext *pContext = (IOContext *)context;

      pContext->tick = tick;
      pContext->beginAt = 0;

      if (pDisk) {
        pContext->buffer = (uint8_t *)calloc(BLOCK_SIZE, 1);

        pContext->dma->read(0, (uint64_t)BLOCK_SIZE, pContext->buffer,
                            dmaDone, context);
      }
      else {
        pContext->dma->read(0, (uint64_t)BLOCK_SIZE, nullptr, dmaDone,
                            context);
      }

      pParent->writeIMS(this, pContext->lpn, pContext->nlpn, dmaDone, context);
    };

    IOContext *pContext = new IOContext(func, resp);

    pContext->beginAt = getTick();
    pContext->lpn = LBN2LPN(lbn);
    pContext->nlpn = IMS_PAGE_NUM;
    pContext->lbn = lbn;
    debugprint(LOG_IMS,
              "NVM     | WRITE_SSTABLE | IOContext | LPN: %ld (LBN: %ld)| number of LPN: %ld",pContext->lpn ,pContext->lbn,pContext->nlpn);

    CPUContext *pCPU =
        new CPUContext(doread, pContext, CPU::NVME__NAMESPACE, CPU::WRITE);

    if (req.useSGL) {
      pContext->dma =
          new SGL(cfgdata, cpuHandler, pCPU, req.entry.data1, req.entry.data2);
    }
    else {
      pContext->dma =
          new PRPList(cfgdata, cpuHandler, pCPU, req.entry.data1,
                      req.entry.data2, (uint64_t)BLOCK_SIZE);
    }
  }
  else {
    func(resp);
  }
}

void Namespace::read_sstable(SQEntryWrapper &req, RequestFunction &func) {
  bool err = false;

  CQEntryWrapper resp(req);
  // char buf[25] = {0};
  // uint32_t dwords[5] = {
  //   req.entry.dword11,
  //   req.entry.dword12,
  //   req.entry.dword13,
  //   req.entry.dword14,
  //   req.entry.dword15
  // };
  // memcpy(buf, dwords, sizeof(dwords));
  // std::string filename(buf);
  // // bool fua = req.entry.dword12 & 0x40000000;
  // hostInfo request(filename);
  uint64_t lbn = INVALIDLBN;
  err = (bool)ims.read_sstable(lbn);
  if (!attached) {
    err = true;
    resp.makeStatus(true, false, TYPE_COMMAND_SPECIFIC_STATUS,
                    STATUS_NAMESPACE_NOT_ATTACHED);
  }
  if(lbn == INVALIDLBN){
    err = true;
    debugprint(LOG_IMS,
             "NVM     | READ_SSTABLE | Allocate LBN is invalid");
    resp.makeStatus(true, false, TYPE_COMMAND_SPECIFIC_STATUS,
                    STATUS_LBN_INVALID);
  }
  if(err){
    debugprint(LOG_IMS,
             "NVM     | READ_SSTABLE | Command failed");
    resp.makeStatus(true, false, TYPE_COMMAND_SPECIFIC_STATUS,
                    STATUS_COMMAND_FAILD);
  }

  if (!err) {
    DMAFunction doRead = [this](uint64_t tick, void *context) {
      DMAFunction dmaDone = [this](uint64_t tick, void *context) {
        IOContext *pContext = (IOContext *)context;
        pContext->beginAt++;

        if (pContext->beginAt == 1) {
          debugprint(
              LOG_HIL_NVME,
              "NVM     | READ_SSTABLE  | CQ %u | SQ %u:%u | CID %u | NSID %-5d | "
              "%" PRIX64 " + %d | %" PRIu64 " - %" PRIu64 " (%" PRIu64 ")",
              pContext->resp.cqID, pContext->resp.entry.dword2.sqID,
              pContext->resp.sqUID, pContext->resp.entry.dword3.commandID, nsid,
              pContext->slba, pContext->nlb, pContext->tick, tick,
              tick - pContext->tick);

          pContext->function(pContext->resp);

          if (pContext->buffer) {
            free(pContext->buffer);
          }

          delete pContext->dma;
          delete pContext;
        }
      };

      IOContext *pContext = (IOContext *)context;

      pContext->tick = tick;
      pContext->beginAt = 0;

      // pParent->readIMS(this, pContext->lpn, pContext->nlpn, dmaDone, pContext);

      pContext->buffer = (uint8_t *)calloc(BLOCK_SIZE, 1);

      if (pDisk) {
        pDisk->readBlock(pContext->lbn, pContext->buffer);
      }
      
      pContext->dma->write(0, (uint64_t)BLOCK_SIZE, pContext->buffer,
                           dmaDone, context);
    };

    IOContext *pContext = new IOContext(func, resp);

    pContext->beginAt = getTick();
    pContext->lpn = LBN2LPN(lbn);
    pContext->nlpn = IMS_PAGE_NUM;
    pContext->lbn = lbn;
    debugprint(LOG_IMS,
              "NVM     | READ_SSTABLE | IOContext | LPN: %ld (LBN: %ld)| number of LPN: %ld",pContext->lpn ,lbn,pContext->nlpn);


    CPUContext *pCPU =
        new CPUContext(doRead, pContext, CPU::NVME__NAMESPACE, CPU::READ);

    if (req.useSGL) {
      pContext->dma =
          new SGL(cfgdata, cpuHandler, pCPU, req.entry.data1, req.entry.data2);
    }
    else {
      pContext->dma =
          new PRPList(cfgdata, cpuHandler, pCPU, req.entry.data1,
                      req.entry.data2, (uint64_t)BLOCK_SIZE);
    }
  }
  else {
    func(resp);
  }
}

void Namespace::read_log(SQEntryWrapper &req, RequestFunction &func) {
  bool err = false;

  CQEntryWrapper resp(req);

  uint64_t lpn = ((uint64_t)req.entry.reserved2) << 32 | req.entry.reserved1;
  // uint8_t *buffer  = new uint8_t[2]; // dummy buffer not real data
  // err = (bool)ims.read_log(lpn,buffer);
  if (!attached) {
    err = true;
    resp.makeStatus(true, false, TYPE_COMMAND_SPECIFIC_STATUS,
                    STATUS_NAMESPACE_NOT_ATTACHED);
  }
  // if(err){
  //   debugprint(LOG_IMS,
  //            "NVM     | READ LOG | Command failed");
  //   resp.makeStatus(true, false, TYPE_COMMAND_SPECIFIC_STATUS,
  //                   STATUS_COMMAND_FAILD);
  // }
  debugprint(LOG_IMS,
             "NVM     | READ LOG | LBN: %lu | LPN: %lu",LPN2LBN(lpn),lpn);

  if (!err) {
    DMAFunction doRead = [this](uint64_t tick, void *context) {
      DMAFunction dmaDone = [this](uint64_t tick, void *context) {
        IOContext *pContext = (IOContext *)context;
        pContext->beginAt++;

        if (pContext->beginAt == 2) {
          debugprint(
              LOG_HIL_NVME,
              "NVM     | READ LOG  | CQ %u | SQ %u:%u | CID %u | NSID %-5d | "
              "%" PRIX64 " + %d | %" PRIu64 " - %" PRIu64 " (%" PRIu64 ")",
              pContext->resp.cqID, pContext->resp.entry.dword2.sqID,
              pContext->resp.sqUID, pContext->resp.entry.dword3.commandID, nsid,
              pContext->slba, pContext->nlb, pContext->tick, tick,
              tick - pContext->tick);

          pContext->function(pContext->resp);

          if (pContext->buffer) {
            free(pContext->buffer);
          }

          delete pContext->dma;
          delete pContext;
        }
      };

      IOContext *pContext = (IOContext *)context;

      pContext->tick = tick;
      pContext->beginAt = 0;

      pParent->readIMS(this, pContext->lpn, pContext->nlpn, dmaDone, pContext);

      pContext->buffer = (uint8_t *)calloc(IMS_PAGE_SIZE, 1);

      if (pDisk) {
        pDisk->readPage(pContext->lpn, pContext->buffer);
      }
      
      pContext->dma->write(0, (uint64_t)IMS_PAGE_SIZE, pContext->buffer,
                           dmaDone, context);
    };

    IOContext *pContext = new IOContext(func, resp);

    pContext->beginAt = getTick();
    pContext->lpn = lpn;
    pContext->nlpn = 1;
    pContext->lbn = LPN2LBN(lpn);
    debugprint(LOG_IMS,
              "NVM     | READ LOG | IOContext | LPN: %ld (LBN: %ld)| number of LPN: %ld",pContext->lpn ,pContext->lbn,pContext->nlpn);


    CPUContext *pCPU =
        new CPUContext(doRead, pContext, CPU::NVME__NAMESPACE, CPU::READ);

    if (req.useSGL) {
      pContext->dma =
          new SGL(cfgdata, cpuHandler, pCPU, req.entry.data1, req.entry.data2);
    }
    else {
      debugprint(LOG_IMS,
              "NVM     | READ LOG | req.entry.data1 : 0x%x",req.entry.data1);
      debugprint(LOG_IMS,
              "NVM     | READ LOG | req.entry.data2 : 0x%x",req.entry.data2);
      pContext->dma =
          new PRPList(cfgdata, cpuHandler, pCPU, req.entry.data1,
                      req.entry.data2, (uint64_t)IMS_PAGE_SIZE);
    }
    if (!pContext->dma) {
      debugprint(LOG_IMS, "FATAL: PRPList allocation failed!");
      abort();
    } 
    else {
      debugprint(LOG_IMS, "PRPList created: %p", pContext->dma);
    }
  }
  else {
    func(resp);
  }
}

void Namespace::read_block(SQEntryWrapper &req, RequestFunction &func) {
  bool err = false;

  CQEntryWrapper resp(req);

  uint32_t lbn = req.entry.dword12;
  // uint8_t *buffer  = new uint8_t[2]; // dummy buffer not real data
  // err = (bool)ims.read_log(lpn,buffer);
  if (!attached) {
    err = true;
    resp.makeStatus(true, false, TYPE_COMMAND_SPECIFIC_STATUS,
                    STATUS_NAMESPACE_NOT_ATTACHED);
  }
  if(err){
    debugprint(LOG_IMS,
             "NVM     | READ LOG | Command failed");
    resp.makeStatus(true, false, TYPE_COMMAND_SPECIFIC_STATUS,
                    STATUS_COMMAND_FAILD);
  }
  debugprint(LOG_IMS,
             "NVM     | READ LOG | LBN: %lu",lbn);

  if (!err) {
    DMAFunction doRead = [this](uint64_t tick, void *context) {
      DMAFunction dmaDone = [this](uint64_t tick, void *context) {
        IOContext *pContext = (IOContext *)context;
        pContext->beginAt++;

        if (pContext->beginAt == 2) {
          debugprint(
              LOG_HIL_NVME,
              "NVM     | READ LOG  | CQ %u | SQ %u:%u | CID %u | NSID %-5d | "
              "%" PRIX64 " + %d | %" PRIu64 " - %" PRIu64 " (%" PRIu64 ")",
              pContext->resp.cqID, pContext->resp.entry.dword2.sqID,
              pContext->resp.sqUID, pContext->resp.entry.dword3.commandID, nsid,
              pContext->slba, pContext->nlb, pContext->tick, tick,
              tick - pContext->tick);

          pContext->function(pContext->resp);

          if (pContext->buffer) {
            free(pContext->buffer);
          }

          delete pContext->dma;
          delete pContext;
        }
      };

      IOContext *pContext = (IOContext *)context;

      pContext->tick = tick;
      pContext->beginAt = 0;

      pParent->readIMS(this, pContext->lpn, pContext->nlpn, dmaDone, pContext);

      pContext->buffer = (uint8_t *)calloc(BLOCK_SIZE, 1);

      if (pDisk) {
        pDisk->readBlock(pContext->lbn, pContext->buffer);
      }
      
      pContext->dma->write(0, (uint64_t)BLOCK_SIZE, pContext->buffer,
                           dmaDone, context);
    };

    IOContext *pContext = new IOContext(func, resp);

    pContext->beginAt = getTick();
    pContext->nlpn = IMS_PAGE_NUM;
    pContext->lpn = LBN2LPN(lbn);
    pContext->lbn = lbn;
    debugprint(LOG_IMS,
              "NVM     | READ LOG | IOContext | LPN:%ld (LBN: %ld) | number of LPN: %ld",pContext->lpn,pContext->lbn,pContext->nlpn);


    CPUContext *pCPU =
        new CPUContext(doRead, pContext, CPU::NVME__NAMESPACE, CPU::READ);

    if (req.useSGL) {
      pContext->dma =
          new SGL(cfgdata, cpuHandler, pCPU, req.entry.data1, req.entry.data2);
    }
    else {
      debugprint(LOG_IMS,
              "NVM     | READ LOG | req.entry.data1 : 0x%x",req.entry.data1);
      debugprint(LOG_IMS,
              "NVM     | READ LOG | req.entry.data2 : 0x%x",req.entry.data2);
      pContext->dma =
          new PRPList(cfgdata, cpuHandler, pCPU, req.entry.data1,
                      req.entry.data2, (uint64_t)BLOCK_SIZE);
    }
    // if (!pContext->dma) {
    //   debugprint(LOG_IMS, "FATAL: PRPList allocation failed!");
    //   abort();
    // } 
    // else {
    //   debugprint(LOG_IMS, "PRPList created: %p", pContext->dma);
    // }
  }
  else {
    func(resp);
  }
}


void Namespace::read_sskeyrange(SQEntryWrapper &req, RequestFunction &func) {
  bool err = false;

  CQEntryWrapper resp(req);
  // char buf[25] = {0};
  // uint32_t dwords[5] = {
  //   req.entry.dword11,
  //   req.entry.dword12,
  //   req.entry.dword13,
  //   req.entry.dword14,
  //   req.entry.dword15
  // };
  // memcpy(buf, dwords, sizeof(dwords));
  // std::string filename(buf);
  // // bool fua = req.entry.dword12 & 0x40000000;
  // hostInfo request(filename);
  uint64_t lpn = INVALID_64;
  err = (bool)ims.read_ssKeyRange(lpn);
  if (!attached) {
    err = true;
    resp.makeStatus(true, false, TYPE_COMMAND_SPECIFIC_STATUS,
                    STATUS_NAMESPACE_NOT_ATTACHED);
  }
  if(lpn == INVALID_64){
    err = true;
    debugprint(LOG_IMS,
             "NVM     | READ_SSKeyRnage | Allocate LBN is invalid");
    resp.makeStatus(true, false, TYPE_COMMAND_SPECIFIC_STATUS,
                    STATUS_LBN_INVALID);
  }
  if(err){
    debugprint(LOG_IMS,
             "NVM     | READ_SSKeyRnage | Command failed");
    resp.makeStatus(true, false, TYPE_COMMAND_SPECIFIC_STATUS,
                    STATUS_COMMAND_FAILD);
  }

  if (!err) {
    DMAFunction doRead = [this](uint64_t tick, void *context) {
      DMAFunction dmaDone = [this](uint64_t tick, void *context) {
        IOContext *pContext = (IOContext *)context;
        pContext->beginAt++;

        if (pContext->beginAt == 2) {
          debugprint(
              LOG_HIL_NVME,
              "NVM     | READ_SSKeyRnage  | CQ %u | SQ %u:%u | CID %u | NSID %-5d | "
              "%" PRIX64 " + %d | %" PRIu64 " - %" PRIu64 " (%" PRIu64 ")",
              pContext->resp.cqID, pContext->resp.entry.dword2.sqID,
              pContext->resp.sqUID, pContext->resp.entry.dword3.commandID, nsid,
              pContext->slba, pContext->nlb, pContext->tick, tick,
              tick - pContext->tick);

          pContext->function(pContext->resp);

          if (pContext->buffer) {
            free(pContext->buffer);
          }

          delete pContext->dma;
          delete pContext;
        }
      };

      IOContext *pContext = (IOContext *)context;

      pContext->tick = tick;
      pContext->beginAt = 0;

      pParent->readIMS(this, pContext->lpn, pContext->nlpn, dmaDone, pContext);

      pContext->buffer = (uint8_t *)calloc(IMS_PAGE_SIZE, 1);

      if (pDisk) {
        pDisk->readPage(pContext->lpn, pContext->buffer);
      }
      
      pContext->dma->write(0, (uint64_t)IMS_PAGE_SIZE, pContext->buffer,
                           dmaDone, context);
    };

    IOContext *pContext = new IOContext(func, resp);

    pContext->beginAt = getTick();
    pContext->lpn = lpn;
    pContext->nlpn = 1;
    debugprint(LOG_IMS,
              "NVM     | READ_SSKeyRnage | IOContext | LPN: %ld | number of LPN: %ld",pContext->lpn ,pContext->nlpn);


    CPUContext *pCPU =
        new CPUContext(doRead, pContext, CPU::NVME__NAMESPACE, CPU::READ);

    if (req.useSGL) {
      pContext->dma =
          new SGL(cfgdata, cpuHandler, pCPU, req.entry.data1, req.entry.data2);
    }
    else {
      pContext->dma =
          new PRPList(cfgdata, cpuHandler, pCPU, req.entry.data1,
                      req.entry.data2, (uint64_t)IMS_PAGE_SIZE);
    }
  }
  else {
    func(resp);
  }
}


void Namespace::read_ssPage(SQEntryWrapper &req, RequestFunction &func) {
  bool err = false;

  CQEntryWrapper resp(req);
  uint32_t page_offset = req.entry.dword12;
  uint32_t page_num = req.entry.dword13;
  // char buf[25] = {0};
  // uint32_t dwords[5] = {
  //   req.entry.dword11,
  //   req.entry.dword12,
  //   req.entry.dword13,
  //   req.entry.dword14,
  //   req.entry.dword15
  // };
  // memcpy(buf, dwords, sizeof(dwords));
  // std::string filename(buf);
  // // bool fua = req.entry.dword12 & 0x40000000;
  // hostInfo request(filename);
  uint64_t lpn = INVALID_64;
  err = (bool)ims.read_ssPage(lpn);
  if (!attached) {
    err = true;
    resp.makeStatus(true, false, TYPE_COMMAND_SPECIFIC_STATUS,
                    STATUS_NAMESPACE_NOT_ATTACHED);
  }
  if(lpn == INVALID_64){
    err = true;
    debugprint(LOG_IMS,
             "NVM     | READ_SSTABLE_PAGE | Allocate LBN is invalid");
    resp.makeStatus(true, false, TYPE_COMMAND_SPECIFIC_STATUS,
                    STATUS_LBN_INVALID);
  }
  if(err){
    debugprint(LOG_IMS,
             "NVM     | READ_SSTABLE_PAGE | Command failed");
    resp.makeStatus(true, false, TYPE_COMMAND_SPECIFIC_STATUS,
                    STATUS_COMMAND_FAILD);
  }

  if (!err) {
    DMAFunction doRead = [this](uint64_t tick, void *context) {
      DMAFunction dmaDone = [this](uint64_t tick, void *context) {
        IOContext *pContext = (IOContext *)context;
        pContext->beginAt++;

        if (pContext->beginAt == 2) {
          debugprint(
              LOG_HIL_NVME,
              "NVM     | READ_SSTABLE_PAGE  | CQ %u | SQ %u:%u | CID %u | NSID %-5d | "
              "%" PRIX64 " + %d | %" PRIu64 " - %" PRIu64 " (%" PRIu64 ")",
              pContext->resp.cqID, pContext->resp.entry.dword2.sqID,
              pContext->resp.sqUID, pContext->resp.entry.dword3.commandID, nsid,
              pContext->slba, pContext->nlb, pContext->tick, tick,
              tick - pContext->tick);

          pContext->function(pContext->resp);

          if (pContext->buffer) {
            free(pContext->buffer);
          }

          delete pContext->dma;
          delete pContext;
        }
      };

      IOContext *pContext = (IOContext *)context;

      pContext->tick = tick;
      pContext->beginAt = 0;

      pParent->readIMS(this, pContext->lpn, pContext->nlpn, dmaDone, pContext);

      pContext->buffer = (uint8_t *)calloc(IMS_PAGE_SIZE*pContext->nlpn, 1);

      if (pDisk) {
        uint64_t readLPN = pContext->lpn + pContext->lbn;
        for(uint64_t i = 0;i < pContext->nlpn;i++){
          pDisk->readPage(readLPN + i,pContext->buffer + (uint64_t)i * IMS_PAGE_SIZE);
        }
      }
      
      pContext->dma->write(0, (uint64_t)(IMS_PAGE_SIZE*pContext->nlpn), pContext->buffer,
                           dmaDone, context);
    };

    IOContext *pContext = new IOContext(func, resp);

    pContext->beginAt = getTick();
    pContext->lpn = lpn+page_offset;
    pContext->nlpn = page_num;
    debugprint(LOG_IMS,
              "NVM     | READ_SSTABLE_PAGE | IOContext | LPN: %ld | number of LPN: %ld",pContext->lpn ,pContext->nlpn);


    CPUContext *pCPU =
        new CPUContext(doRead, pContext, CPU::NVME__NAMESPACE, CPU::READ);

    if (req.useSGL) {
      pContext->dma =
          new SGL(cfgdata, cpuHandler, pCPU, req.entry.data1, req.entry.data2);
    }
    else {
      pContext->dma =
          new PRPList(cfgdata, cpuHandler, pCPU, req.entry.data1,
                      req.entry.data2, (uint64_t)(IMS_PAGE_SIZE*page_num));
    }
  }
  else {
    func(resp);
  }
}


void Namespace::init_IMS(SQEntryWrapper &req, RequestFunction &func) {
  bool err = false;

  CQEntryWrapper resp(req);


  if (!attached) {
    err = true;
    resp.makeStatus(true, false, TYPE_COMMAND_SPECIFIC_STATUS,
                    STATUS_NAMESPACE_NOT_ATTACHED);
  }
  err = (bool)ims.init_IMS();
  if (pDisk){
    debugprint(LOG_IMS,
              "NVM     | Init_IMS start");
  #if RUNTYPE
    // ims.disk_ = *pDisk;
  #endif
  }
  else{
    err = true;
    debugprint(LOG_IMS,
             "NVM     | Init_IMS failed, pDisk is null");
  }
  if(!err) {
    debugprint(LOG_IMS,
             "NVM     | Init_IMS success");
    resp.makeStatus(false, false, TYPE_GENERIC_COMMAND_STATUS,
                    STATUS_SUCCESS);
  }
  else {
    debugprint(LOG_IMS, "NVM     | Init_IMS failed");
    resp.makeStatus(false, false, TYPE_GENERIC_COMMAND_STATUS,
                    STATUS_IMS_INIT_FAILED);
  }
  
  func(resp);
}

void Namespace::close_IMS(SQEntryWrapper &req, RequestFunction &func) {
  bool err = false;

  CQEntryWrapper resp(req);


  if (!attached) {
    err = true;
    resp.makeStatus(true, false, TYPE_COMMAND_SPECIFIC_STATUS,
                    STATUS_NAMESPACE_NOT_ATTACHED);
  }

  debugprint(LOG_IMS,
             "NVM     | Close_IMS start");
  err = (bool)ims.close_IMS();
  if(!err) {
    debugprint(LOG_IMS, "NVM     | Close_IMS success");
    resp.makeStatus(false, false, TYPE_GENERIC_COMMAND_STATUS,
                    STATUS_SUCCESS);
  }
  else {
    debugprint(LOG_IMS,
             "NVM     | Close_IMS failed");
    resp.makeStatus(false, false, TYPE_GENERIC_COMMAND_STATUS,
                    STATUS_IMS_INIT_FAILED);
    
  }
  
  func(resp);
}

// void Namespace::monitor_IMS(SQEntryWrapper &req, RequestFunction &func) {
//   MonitorType type = static_cast<MonitorType>(req.entry.dword13);
//   CQEntryWrapper resp(req);

//   debugprint(LOG_IMS,
//              "NVM     | Monitor IMS start");
//   switch(type) {

//     case MonitorType::DUMP_MAPPING_INFO:
//       mappingManager.dump_mapping();
//       resp.makeStatus(false, false, TYPE_GENERIC_COMMAND_STATUS,
//                     STATUS_SUCCESS);
//       break;

//     case MonitorType::DUMP_LBNPOOL_INFO:
//       lbnPoolManager.dump_LBNPool();
//       resp.makeStatus(false, false, TYPE_GENERIC_COMMAND_STATUS,
//                     STATUS_SUCCESS);
//       break;

//     default:
//       debugprint(LOG_IMS,
//              "NVM     | monitor IMS | error type : %d",type);
//       resp.makeStatus(false, false, TYPE_COMMAND_SPECIFIC_STATUS,
//                       STATUS_MONITOR_FAILD);
//       break;
//   }
//   func(resp);
// }

void Namespace::allocate_lbn(SQEntryWrapper &req, RequestFunction &func) {
  bool err = false;

  CQEntryWrapper resp(req);
  // bool fua = req.entry.dword12 & 0x40000000;
  uint64_t lbn = INVALIDLBN;
  err = (bool)ims.allocate_block(&lbn);
  if (!attached) {
    err = true;
    resp.makeStatus(true, false, TYPE_COMMAND_SPECIFIC_STATUS,
                    STATUS_NAMESPACE_NOT_ATTACHED);
  }
  if(lbn == INVALIDLBN){
    err = true;
  }
  if(err){
    debugprint(LOG_IMS,
             "NVM     | ALLOCATE LBN | Command failed");
    resp.makeStatus(true, false, TYPE_COMMAND_SPECIFIC_STATUS,
                    STATUS_COMMAND_FAILD);
  }
  else{
    debugprint(LOG_IMS,
             "NVM     | ALLOCATE LBN | allocate LBN is: %lu",lbn);
  }
  

  if (!err) {
    DMAFunction doRead = [this](uint64_t tick, void *context) {
      DMAFunction dmaDone = [this](uint64_t tick, void *context) {
        IOContext *pContext = (IOContext *)context;
        pContext->beginAt++;
        if (pContext->beginAt == 1) {
          debugprint(
              LOG_HIL_NVME,
              "NVM     | ALLOCATE LBN  | CQ %u | SQ %u:%u | CID %u | NSID %-5d | "
              "%" PRIX64 " + %d | %" PRIu64 " - %" PRIu64 " (%" PRIu64 ")",
              pContext->resp.cqID, pContext->resp.entry.dword2.sqID,
              pContext->resp.sqUID, pContext->resp.entry.dword3.commandID, nsid,
              pContext->slba, pContext->nlb, pContext->tick, tick,
              tick - pContext->tick);

          pContext->function(pContext->resp);

          if (pContext->buffer) {
            free(pContext->buffer);
          }

          delete pContext->dma;
          delete pContext;
        }
      };

      IOContext *pContext = (IOContext *)context;

      pContext->tick = tick;
      pContext->beginAt = 0;
      pContext->buffer = (uint8_t *)calloc(sizeof(uint64_t), 1);
      memcpy(pContext->buffer, &pContext->lbn, sizeof(uint64_t));
      pContext->dma->write(0, sizeof(uint64_t), pContext->buffer,
                           dmaDone, context);
    };

    IOContext *pContext = new IOContext(func, resp);

    pContext->beginAt = getTick();
    pContext->lbn = lbn;
    CPUContext *pCPU =
        new CPUContext(doRead, pContext, CPU::NVME__NAMESPACE, CPU::READ);

    if (req.useSGL) {
      pContext->dma =
          new SGL(cfgdata, cpuHandler, pCPU, req.entry.data1, req.entry.data2);
    }
    else {
      pContext->dma =
          new PRPList(cfgdata, cpuHandler, pCPU, req.entry.data1,
                      req.entry.data2, (uint64_t)sizeof(uint64_t));
    }
  }
  else {
    func(resp);
  }
}


void Namespace::write_buffer(SQEntryWrapper &req, RequestFunction &func) {
  bool err = false;

  CQEntryWrapper resp(req);
  uint32_t numberOfSize = req.entry.dword12;
  if (!attached) {
    err = true;
    resp.makeStatus(true, false, TYPE_COMMAND_SPECIFIC_STATUS,
                    STATUS_NAMESPACE_NOT_ATTACHED);
  }
  if(err){
    debugprint(LOG_IMS,
             "NVM     | WRITE_BUFFER | Command failed");
    resp.makeStatus(true, false, TYPE_COMMAND_SPECIFIC_STATUS,
                    STATUS_COMMAND_FAILD);
  }
  

  if (!err) {
    DMAFunction doRead = [this](uint64_t tick, void *context) {
      DMAFunction dmaDone = [this](uint64_t tick, void *context) {
        IOContext *pContext = (IOContext *)context;
        pContext->beginAt++;
        if (pContext->beginAt == 1) {
          debugprint(
              LOG_IMS,
              "NVM     | WRITE_BUFFER | CQ %u | SQ %u:%u | CID %u | NSID %-5d | %" PRIu64 " - %" PRIu64 " (%" PRIu64 ")",
              pContext->resp.cqID, pContext->resp.entry.dword2.sqID,
              pContext->resp.sqUID, pContext->resp.entry.dword3.commandID, nsid,
              pContext->tick, tick,tick - pContext->tick);

          pContext->function(pContext->resp);

          if (pContext->buffer) {
            int err = ims.write_meta(pContext->buffer,pContext->nlb);
            free(pContext->buffer);
          }

          delete pContext->dma;
          delete pContext;
        }
      };

      IOContext *pContext = (IOContext *)context;

      pContext->tick = tick;
      pContext->beginAt = 0;
      pContext->buffer = (uint8_t *)calloc(pContext->nlb, sizeof(uint8_t));
      pContext->dma->read(0, pContext->nlb, pContext->buffer,
                            dmaDone, context);
    };

    IOContext *pContext = new IOContext(func, resp);

    pContext->beginAt = getTick();
    // Using nlb to transfer the variable of my data size
    pContext->nlb     = numberOfSize;
    CPUContext *pCPU =
        new CPUContext(doRead, pContext, CPU::NVME__NAMESPACE, CPU::READ);

    if (req.useSGL) {
      pContext->dma =
          new SGL(cfgdata, cpuHandler, pCPU, req.entry.data1, req.entry.data2);
    }
    else {
      pContext->dma =
          new PRPList(cfgdata, cpuHandler, pCPU, req.entry.data1,
                      req.entry.data2, (uint64_t)numberOfSize);
    }
  }
  else {
    func(resp);
  }
}


void Namespace::read_buffer(SQEntryWrapper &req, RequestFunction &func) {
  bool err = false;

  CQEntryWrapper resp(req);
  uint32_t numberOfSize = req.entry.dword12;
  if (!attached) {
    err = true;
    resp.makeStatus(true, false, TYPE_COMMAND_SPECIFIC_STATUS,
                    STATUS_NAMESPACE_NOT_ATTACHED);
  }
  if(err){
    debugprint(LOG_IMS,
             "NVM     | READ_BUFFER | Command failed");
    resp.makeStatus(true, false, TYPE_COMMAND_SPECIFIC_STATUS,
                    STATUS_COMMAND_FAILD);
  }
  

  if (!err) {
    DMAFunction doRead = [this](uint64_t tick, void *context) {
      DMAFunction dmaDone = [this](uint64_t tick, void *context) {
        IOContext *pContext = (IOContext *)context;
        pContext->beginAt++;
        if (pContext->beginAt == 1) {
          debugprint(
              LOG_IMS,
              "NVM     | READ_BUFFER | CQ %u | SQ %u:%u | CID %u | NSID %-5d | %" PRIu64 " - %" PRIu64 " (%" PRIu64 ")",
              pContext->resp.cqID, pContext->resp.entry.dword2.sqID,
              pContext->resp.sqUID, pContext->resp.entry.dword3.commandID, nsid,
              pContext->tick, tick,tick - pContext->tick);

          pContext->function(pContext->resp);

          if (pContext->buffer) {
            free(pContext->buffer);
          }

          delete pContext->dma;
          delete pContext;
        }
      };

      IOContext *pContext = (IOContext *)context;

      pContext->tick = tick;
      pContext->beginAt = 0;
      pContext->buffer = (uint8_t *)calloc(pContext->nlb, sizeof(uint8_t));
      int err = ims.read_meta(pContext->buffer,pContext->nlb);
      pContext->dma->write(0, pContext->nlb, pContext->buffer,
                            dmaDone, context);
      
    };

    IOContext *pContext = new IOContext(func, resp);

    pContext->beginAt = getTick();
    // Using nlb to transfer the variable of my data size
    pContext->nlb     = numberOfSize;
    CPUContext *pCPU =
        new CPUContext(doRead, pContext, CPU::NVME__NAMESPACE, CPU::READ);

    if (req.useSGL) {
      pContext->dma =
          new SGL(cfgdata, cpuHandler, pCPU, req.entry.data1, req.entry.data2);
    }
    else {
      pContext->dma =
          new PRPList(cfgdata, cpuHandler, pCPU, req.entry.data1,
                      req.entry.data2, (uint64_t)numberOfSize);
    }
  }
  else {
    func(resp);
  }
}


void Namespace::open_DB(SQEntryWrapper &req, RequestFunction &func) {
  bool err = false;

  CQEntryWrapper resp(req);
  // bool fua = req.entry.dword12 & 0x40000000;
  uint32_t dataLen = INVALID_32;
  int ret = ims.open_DB(&dataLen);
  if(ret == OPERATION_FAILURE){
    err = true;
  }
  if (!attached) {
    err = true;
    resp.makeStatus(true, false, TYPE_COMMAND_SPECIFIC_STATUS,
                    STATUS_NAMESPACE_NOT_ATTACHED);
  }
  if(dataLen == INVALID_32){
    err = true;
  }
  if(err){
    debugprint(LOG_IMS,
             "NVM     | OPEN DB | Command failed");
    resp.makeStatus(true, false, TYPE_COMMAND_SPECIFIC_STATUS,
                    STATUS_COMMAND_FAILD);
  }
  else{
    debugprint(LOG_IMS,
             "NVM     | OPEN DB | open DB is success ,need read datalen: %lu",dataLen);
  }

  if (!err) {
    DMAFunction doRead = [this](uint64_t tick, void *context) {
      DMAFunction dmaDone = [this](uint64_t tick, void *context) {
        IOContext *pContext = (IOContext *)context;
        pContext->beginAt++;
        if (pContext->beginAt == 1) {
          debugprint(
              LOG_HIL_NVME,
              "NVM     | OPEN DB  | CQ %u | SQ %u:%u | CID %u | NSID %-5d | "
              "%" PRIX64 " + %d | %" PRIu64 " - %" PRIu64 " (%" PRIu64 ")",
              pContext->resp.cqID, pContext->resp.entry.dword2.sqID,
              pContext->resp.sqUID, pContext->resp.entry.dword3.commandID, nsid,
              pContext->slba, pContext->nlb, pContext->tick, tick,
              tick - pContext->tick);

          pContext->function(pContext->resp);

          if (pContext->buffer) {
            free(pContext->buffer);
          }

          delete pContext->dma;
          delete pContext;
        }
      };

      IOContext *pContext = (IOContext *)context;

      pContext->tick = tick;
      pContext->beginAt = 0;
      pContext->buffer = (uint8_t *)calloc(1,sizeof(uint32_t));
      uint32_t datalen = static_cast<uint32_t>(pContext->nlb);
      memcpy(pContext->buffer, &datalen, sizeof(uint32_t));
      pContext->dma->write(0, sizeof(uint32_t), pContext->buffer,
                           dmaDone, context);
    };

    IOContext *pContext = new IOContext(func, resp);

    pContext->beginAt = getTick();
    pContext->nlb     = static_cast<uint32_t>(dataLen);
    CPUContext *pCPU = 
        new CPUContext(doRead, pContext, CPU::NVME__NAMESPACE, CPU::READ);

    if (req.useSGL) {
      pContext->dma =
          new SGL(cfgdata, cpuHandler, pCPU, req.entry.data1, req.entry.data2);
    }
    else {
      pContext->dma =
          new PRPList(cfgdata, cpuHandler, pCPU, req.entry.data1,
                      req.entry.data2, (uint64_t)sizeof(uint32_t));
    }
  }
  else {
    func(resp);
  }
}


void Namespace::close_DB(SQEntryWrapper &req, RequestFunction &func) {
  bool err = false;

  CQEntryWrapper resp(req);
  uint32_t numberOfSize = req.entry.dword12;
  if (!attached) {
    err = true;
    resp.makeStatus(true, false, TYPE_COMMAND_SPECIFIC_STATUS,
                    STATUS_NAMESPACE_NOT_ATTACHED);
  }
  if(err){
    debugprint(LOG_IMS,
             "NVM     | CLOSE DB | Command failed");
    resp.makeStatus(true, false, TYPE_COMMAND_SPECIFIC_STATUS,
                    STATUS_COMMAND_FAILD);
  }
  
  debugprint(LOG_IMS,
             "NVM     | CLOSE DB | Command pass");
  if (!err) {
    DMAFunction doRead = [this](uint64_t tick, void *context) {
      DMAFunction dmaDone = [this](uint64_t tick, void *context) {
        IOContext *pContext = (IOContext *)context;
        pContext->beginAt++;
        if (pContext->beginAt == 1) {
          if (pContext->buffer) {
            int err = ims.close_DB(pContext->buffer,pContext->nlb);
            free(pContext->buffer);
          }
          debugprint(
              LOG_HIL_NVME,
              "Close DB is done");
          dumpSearchFlashStatsAndReset();
          dumpSearchLayerStatsAndReset();
          pContext->function(pContext->resp);
          delete pContext->dma;
          delete pContext;
        }
      };

      IOContext *pContext = (IOContext *)context;

      pContext->tick = tick;
      pContext->beginAt = 0;
      pContext->buffer = (uint8_t *)calloc(pContext->nlb, sizeof(uint8_t));
      pContext->dma->read(0, pContext->nlb, pContext->buffer,
                            dmaDone, context);
    };

    IOContext *pContext = new IOContext(func, resp);

    pContext->beginAt = getTick();
    // Using nlb to transfer the variable of my data size
    pContext->nlb     = numberOfSize;
    CPUContext *pCPU =
        new CPUContext(doRead, pContext, CPU::NVME__NAMESPACE, CPU::READ);

    if (req.useSGL) {
      pContext->dma =
          new SGL(cfgdata, cpuHandler, pCPU, req.entry.data1, req.entry.data2);
    }
    else {
      pContext->dma =
          new PRPList(cfgdata, cpuHandler, pCPU, req.entry.data1,
                      req.entry.data2, static_cast<uint64_t>(numberOfSize));
    }
  }
  else {
    func(resp);
  }
}


void Namespace::search_key(SQEntryWrapper &req, RequestFunction &func) {
  CQEntryWrapper resp(req);

  if (!attached) {
    resp.makeStatus(true, false,
                    TYPE_COMMAND_SPECIFIC_STATUS,
                    STATUS_NAMESPACE_NOT_ATTACHED);
    func(resp);
    return;
  }

  const uint32_t payloadSize = req.entry.dword12;

  if (payloadSize == 0) {
    resp.makeStatus(true, false,
                    TYPE_COMMAND_SPECIFIC_STATUS,
                    STATUS_COMMAND_FAILD);
    func(resp);
    return;
  }

  debugprint(LOG_IMS,
             "NVM     | SEARCH_KEY | payloadSize=%u data1=%" PRIx64
             " data2=%" PRIx64,
             payloadSize,
             req.entry.data1,
             req.entry.data2);

  DMAFunction doReadPayload = [this](uint64_t tick, void *context) {
    IOContext *pContext = static_cast<IOContext *>(context);

    pContext->tick = tick;

    if (pContext->beginAt == 0) {
      pContext->beginAt = tick;
    }

    pContext->buffer =
        static_cast<uint8_t *>(calloc(pContext->nlb, sizeof(uint8_t)));

    if (pContext->buffer == nullptr) {
      pContext->resp.makeStatus(true, false,
                                TYPE_COMMAND_SPECIFIC_STATUS,
                                STATUS_COMMAND_FAILD);
      pContext->function(pContext->resp);

      delete pContext->dma;
      delete pContext;
      return;
    }

    DMAFunction payloadDone = [this](uint64_t tickAfterDma, void *context) {
      IOContext *pContext = static_cast<IOContext *>(context);

      const uint64_t payload_dma_ticks = tickAfterDma - pContext->tick;

      std::vector<uint64_t> lbn_list;

      const uint64_t decode_begin = tickAfterDma;

      int ret = ims.search_from_buffer(pContext->buffer,
                                        pContext->nlb,
                                        lbn_list);

      const uint64_t decode_end = getTick();
      const uint64_t decode_ticks =
          decode_end >= decode_begin ? decode_end - decode_begin : 0;

      free(pContext->buffer);
      pContext->buffer = nullptr;

      delete pContext->dma;
      pContext->dma = nullptr;

      if (ret == OPERATION_FAILURE) {
        SearchLayerSample sample;
        sample.failed = true;
        sample.cmd_ticks = tickAfterDma - pContext->beginAt;
        sample.payload_dma_ticks = payload_dma_ticks;
        sample.ims_decode_ticks = decode_ticks;
        recordSearchLayerSample(sample);

        pContext->resp.makeStatus(true, false,
                                  TYPE_COMMAND_SPECIFIC_STATUS,
                                  STATUS_COMMAND_FAILD);
        pContext->function(pContext->resp);
        delete pContext;
        return;
      }

      if (lbn_list.empty()) {
        g_search_flash_stats.recordNoFlash();

        SearchLayerSample sample;
        sample.no_flash = true;
        sample.cmd_ticks = tickAfterDma - pContext->beginAt;
        sample.payload_dma_ticks = payload_dma_ticks;
        sample.ims_decode_ticks = decode_ticks;
        sample.nreads = 0;
        sample.rounds = 0;
        recordSearchLayerSample(sample);

        debugprint(LOG_IMS,
                   "NVM     | SEARCH_KEY | no flash IO | "
                   "cmd=%" PRIu64
                   " payload_dma=%" PRIu64
                   " decode=%" PRIu64,
                   sample.cmd_ticks,
                   sample.payload_dma_ticks,
                   sample.ims_decode_ticks);

        pContext->resp.makeStatus(false, false,
                                  TYPE_GENERIC_COMMAND_STATUS,
                                  STATUS_SUCCESS);
        pContext->function(pContext->resp);
        delete pContext;
        return;
      }

      auto st = std::make_shared<SearchKeyState>();

      st->io = pContext;
      st->cmd_begin = pContext->beginAt;
      st->payload_dma_ticks = payload_dma_ticks;
      st->ims_decode_ticks = decode_ticks;

      st->per_ch_lpn.assign(CHANNEL_NUM, {});

      for (auto lbn : lbn_list) {
        const int ch = LBN2CH(lbn);

        if (ch < 0 || ch >= CHANNEL_NUM) {
          debugprint(LOG_IMS,
                     "NVM     | SEARCH_KEY | invalid ch=%d for lbn=%" PRIu64,
                     ch,
                     lbn);
          continue;
        }

        st->per_ch_lpn[ch].push_back(LBN2LPN(lbn));
      }

      st->total_reads = 0;
      st->rounds = 0;

      for (int ch = 0; ch < CHANNEL_NUM; ++ch) {
        st->total_reads += st->per_ch_lpn[ch].size();
        st->rounds = std::max(st->rounds, st->per_ch_lpn[ch].size());
      }

      if (st->total_reads == 0) {
        g_search_flash_stats.recordNoFlash();

        SearchLayerSample sample;
        sample.no_flash = true;
        sample.cmd_ticks = tickAfterDma - pContext->beginAt;
        sample.payload_dma_ticks = payload_dma_ticks;
        sample.ims_decode_ticks = decode_ticks;
        recordSearchLayerSample(sample);

        pContext->resp.makeStatus(false, false,
                                  TYPE_GENERIC_COMMAND_STATUS,
                                  STATUS_SUCCESS);
        pContext->function(pContext->resp);
        delete pContext;
        return;
      }

      st->next_idx.assign(CHANNEL_NUM, 0);
      st->outstanding.assign(CHANNEL_NUM, 0);

      using IssueFn = std::function<void(int, uint64_t)>;

      auto issueNext = std::make_shared<IssueFn>();
      std::weak_ptr<IssueFn> weakIssueNext(issueNext);

      *issueNext = [this, st, weakIssueNext](int ch, uint64_t nowTick) {
        if (ch < 0 || ch >= CHANNEL_NUM) {
          return;
        }

        if (st->outstanding[ch]) {
          return;
        }

        const size_t idx = st->next_idx[ch];

        if (idx >= st->per_ch_lpn[ch].size()) {
          return;
        }

        st->outstanding[ch] = 1;
        st->next_idx[ch] = idx + 1;

        if (!st->flash_issued) {
          st->flash_issued = true;
          st->t_flash_first_issue = nowTick;
          st->t_flash_last_done = nowTick;
        }

        const uint64_t slpn = st->per_ch_lpn[ch][idx];
        const uint64_t nlpn = 1;

        auto holdIssueNext = weakIssueNext.lock();

        DMAFunction readDone =
            [st, ch, weakIssueNext, holdIssueNext](uint64_t doneTick, void *) {
              (void)holdIssueNext;

              st->outstanding[ch] = 0;
              st->completed++;
              st->t_flash_last_done =
                  std::max(st->t_flash_last_done, doneTick);

              if (st->completed == st->total_reads) {
                const uint64_t t_flash =
                    st->t_flash_last_done - st->t_flash_first_issue;

                g_search_flash_stats.recordWithFlash(t_flash);

                st->cmd_end = doneTick;

                SearchLayerSample sample;
                sample.cmd_ticks = st->cmd_end - st->cmd_begin;
                sample.payload_dma_ticks = st->payload_dma_ticks;
                sample.ims_decode_ticks = st->ims_decode_ticks;
                sample.flash_wall_ticks = t_flash;
                sample.nreads = st->total_reads;
                sample.rounds = st->rounds;
                sample.layer = st->layer;

                recordSearchLayerSample(sample);

                const uint64_t layer_sum = st->layer.sum();

               MYDB_LOG("SEARCH_KEY nreads=%zu rounds=%zu cmd=%" PRIu64
                        " payload_dma=%" PRIu64
                        " decode=%" PRIu64
                        " flash_wall=%" PRIu64
                        " HIL=%" PRIu64
                        " ICL=%" PRIu64
                        " FTL=%" PRIu64
                        " PAL=%" PRIu64,
                        st->total_reads,
                        st->rounds,
                        sample.cmd_ticks,
                        sample.payload_dma_ticks,
                        sample.ims_decode_ticks,
                        sample.flash_wall_ticks,
                        st->layer.hil,
                        st->layer.icl,
                        st->layer.ftl,
                        st->layer.pal);

                IOContext *io = st->io;

                io->resp.makeStatus(false, false,
                                    TYPE_GENERIC_COMMAND_STATUS,
                                    STATUS_SUCCESS);
                io->function(io->resp);

                delete io;
                return;
              }

              if (auto fn = weakIssueNext.lock()) {
                (*fn)(ch, doneTick);
              }
            };

        auto layerDone = [st](const SimpleSSD::Prof::Breakdown &b) {
          if (!b.valid) {
            return;
          }

          st->layer.hil += b.excl[SimpleSSD::Prof::L_HIL];
          st->layer.icl += b.excl[SimpleSSD::Prof::L_ICL];
          st->layer.ftl += b.excl[SimpleSSD::Prof::L_FTL];
          st->layer.pal += b.excl[SimpleSSD::Prof::L_PAL];
        };

        pParent->readIMSDirectFTL(this,
                                  slpn,
                                  nlpn,
                                  readDone,
                                  nullptr,
                                  layerDone);
      };

      for (int ch = 0; ch < CHANNEL_NUM; ++ch) {
        (*issueNext)(ch, tickAfterDma);
      }
    };

    pContext->dma->read(0,
                        static_cast<uint64_t>(pContext->nlb),
                        pContext->buffer,
                        payloadDone,
                        context);
  };

  IOContext *pContext = new IOContext(func, resp);

  pContext->nlb = payloadSize;
  pContext->beginAt = getTick();

  CPUContext *pCPU =
      new CPUContext(doReadPayload,
                     pContext,
                     CPU::NVME__NAMESPACE,
                     CPU::READ);

  if (req.useSGL) {
    pContext->dma =
        new SGL(cfgdata,
                cpuHandler,
                pCPU,
                req.entry.data1,
                req.entry.data2);
  }
  else {
    pContext->dma =
        new PRPList(cfgdata,
                    cpuHandler,
                    pCPU,
                    req.entry.data1,
                    req.entry.data2,
                    static_cast<uint64_t>(payloadSize));
  }
}

void Namespace::erase_sstable(SQEntryWrapper &req, RequestFunction &func) {
  bool err = false;

  CQEntryWrapper resp(req);

  if (!attached) {
    err = true;
    resp.makeStatus(true, false, TYPE_COMMAND_SPECIFIC_STATUS,
                    STATUS_NAMESPACE_NOT_ATTACHED);
  }

  uint64_t lbn = INVALIDLBN;
  if (!err) {
    int ret = ims.erase_sstable(lbn);

    if (ret != OPERATION_SUCCESS || lbn == INVALIDLBN) {
      err = true;
      debugprint(LOG_IMS,
                 "NVM     | ERASE_SSTABLE | IMS erase_sstable failed (ret=%d, lbn=%ld)",
                 ret, (long)lbn);
      resp.makeStatus(true, false, TYPE_COMMAND_SPECIFIC_STATUS,
                      STATUS_COMMAND_FAILD);
    }
  }

  if (err) {
    func(resp);
    return;
  }
  // IMS LBN -> FTL LPN range
  uint64_t slpn = LBN2LPN(lbn);
  uint64_t nlpn = IMS_PAGE_NUM;   // 一個 IMS block 占用多少 page

  debugprint(LOG_IMS,
             "NVM     | ERASE_SSTABLE | SQ %u:%u | CID %u | NSID %-5d | "
             "LBN %ld | LPN %ld + %ld",
             req.sqID, req.sqUID, req.entry.dword0.commandID, nsid,
             (long)lbn, (long)slpn, (long)nlpn);

  // 當 FTL trim 完成後要做的事：打 CQ + free context
  DMAFunction doTrimDone = [this](uint64_t tick, void *context) {
    IOContext *pContext = (IOContext *)context;

    debugprint(
        LOG_IMS,
        "NVM     | ERASE_SSTABLE | CQ %u | SQ %u:%u | CID %u | NSID %-5d | "
        "LBN %ld | LPN %ld + %ld | %" PRIu64 " - %" PRIu64 " (%" PRIu64 ")",
        pContext->resp.cqID,
        pContext->resp.entry.dword2.sqID,
        pContext->resp.sqUID,
        pContext->resp.entry.dword3.commandID,
        nsid,
        (long)pContext->lbn,
        (long)pContext->lpn,
        (long)pContext->nlpn,
        pContext->beginAt,
        tick,
        tick - pContext->beginAt);

    pContext->function(pContext->resp);

    delete pContext;
  };

  IOContext *pContext = new IOContext(func, resp);
  pContext->beginAt = getTick();
  pContext->lbn     = lbn;
  pContext->lpn     = slpn;
  pContext->nlpn    = nlpn;

  pParent->trimIMS(this, slpn, nlpn, doTrimDone, pContext);
}


void Namespace::compaction_io(SQEntryWrapper &req, RequestFunction &func) {
  CQEntryWrapper resp(req);

  // 1) attached 檢查
  if (!attached) {
    resp.makeStatus(true, false,
                    TYPE_COMMAND_SPECIFIC_STATUS,
                    STATUS_NAMESPACE_NOT_ATTACHED);
    debugprint(LOG_IMS,
               "NVM     | Compaction IO sim | Command failed (namespace not attached)");
    func(resp);
    return;
  }

  // 2) 讓 compaction_io 進入事件模型（很重要！）
  DMAFunction doCompaction = [this](uint64_t tick, void *context) {
    IOContext *pContext = static_cast<IOContext*>(context);

    // 用 tick 當整個 compaction_io 的起點（跟你 search_key 一樣）
    pContext->tick    = tick;
    pContext->beginAt = 0;   // 這裡 beginAt 當 completed counter 用

    std::vector<uint64_t> lbn_list;
    int ret = ims.simulate_compaction_io(lbn_list);

    if (ret == OPERATION_FAILURE) {
      debugprint(LOG_IMS,
                 "NVM     | Compaction IO sim | ims.simulate_compaction_io() failed");
      pContext->resp.makeStatus(true, false,
                                TYPE_COMMAND_SPECIFIC_STATUS,
                                STATUS_COMMAND_FAILD);
      pContext->function(pContext->resp);

      if (pContext->buffer) { free(pContext->buffer); pContext->buffer = nullptr; }
      if (pContext->dma)    { delete pContext->dma;   pContext->dma = nullptr; }

      delete pContext;
      return;
    }

    if (lbn_list.empty()) {
      debugprint(LOG_IMS,
                 "NVM     | Compaction IO sim | lbn_list is empty");
      pContext->resp.makeStatus(false, false,
                                TYPE_GENERIC_COMMAND_STATUS,
                                STATUS_SUCCESS);
      pContext->function(pContext->resp);

      if (pContext->buffer) { free(pContext->buffer); pContext->buffer = nullptr; }
      if (pContext->dma)    { delete pContext->dma;   pContext->dma = nullptr; }

      delete pContext;
      return;
    }

    pContext->nlb = lbn_list.size();

    debugprint(LOG_IMS,
               "NVM     | Compaction IO sim | simulate_compaction_io() success, nlb=%zu",
               lbn_list.size());

    DMAFunction doneOne = [this](uint64_t doneTick, void *ctx) {
      IOContext *pContext = static_cast<IOContext*>(ctx);

      pContext->beginAt++;

      if (pContext->beginAt == pContext->nlb) {
        pContext->resp.makeStatus(false, false,
                                  TYPE_GENERIC_COMMAND_STATUS,
                                  STATUS_SUCCESS);
        pContext->function(pContext->resp);

        debugprint(
          LOG_IMS,
          "NVM     | Compaction IO sim | CQ %u | SQ %u:%u | CID %u | NSID %-5d | %" PRIu64 " - %" PRIu64 " (%" PRIu64 ")",
          pContext->resp.cqID, pContext->resp.entry.dword2.sqID,
          pContext->resp.sqUID, pContext->resp.entry.dword3.commandID,
          nsid, pContext->tick, doneTick, doneTick - pContext->tick);

        if (pContext->buffer) { free(pContext->buffer); pContext->buffer = nullptr; }
        if (pContext->dma)    { delete pContext->dma;   pContext->dma = nullptr; }

        delete pContext;
      }
    };
    std::vector<std::vector<uint64_t>> per_ch(CHANNEL_NUM);
    per_ch.reserve(CHANNEL_NUM);

    for (auto lbn : lbn_list) {
      per_ch[LBN2CH(lbn)].push_back(lbn);
    }

    size_t issued = 0;
    size_t round  = 0;
    while (issued < lbn_list.size()) {
      bool progressed = false;

      for (int ch = 0; ch < CHANNEL_NUM; ++ch) {
        if (round < per_ch[ch].size()) {
          uint64_t lbn  = per_ch[ch][round];
          uint64_t slpn = LBN2LPN(lbn);
          uint64_t nlpn = IMS_PAGE_NUM;

          debugprint(LOG_IMS,
            "NVM     | Compaction IO sim | Read SStable in LBN:%llu in CH[%d] | Issued %zu/%zu",
            (unsigned long long)lbn, ch, issued + 1, lbn_list.size());
          pParent->readIMS(this, slpn, nlpn, doneOne, pContext);

          issued++;
          progressed = true;
        }
      }

      if (!progressed) break;
      round++;
    }
  };

  IOContext *pContext = new IOContext(func, resp);
  execute(CPU::NVME__NAMESPACE, CPU::READ, doCompaction, pContext);
}



void Namespace::trival_move(SQEntryWrapper &req, RequestFunction &func) {
  bool err = false;

  CQEntryWrapper resp(req);

  if (!attached) {
    err = true;
    resp.makeStatus(true, false, TYPE_COMMAND_SPECIFIC_STATUS,
                    STATUS_NAMESPACE_NOT_ATTACHED);
  }

  if (!err) {
    int ret = ims.trivial_move();

    if (ret != OPERATION_SUCCESS) {
      err = true;
      debugprint(LOG_IMS,
                 "NVM     | TRIVAL_MOVE | IMS trival_move failed (ret=%d)",ret);
      resp.makeStatus(true, false, TYPE_COMMAND_SPECIFIC_STATUS,
                      STATUS_COMMAND_FAILD);
    }
  }

  if (err) {
    func(resp);
    return;
  }
  debugprint(LOG_IMS,
             "NVM     | TRIVAL_MOVE | SQ %u:%u | CID %u | NSID %-5d",
             req.sqID, req.sqUID, req.entry.dword0.commandID, nsid);
  func(resp);
}

}  // namespace NVMe

}  // namespace HIL

}  // namespace SimpleSSD
