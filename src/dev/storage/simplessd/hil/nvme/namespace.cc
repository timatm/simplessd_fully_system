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


#include "ims/include/lbn_pool.hh"
#include "ims/include/mapping_table.hh"
#include "ims/include/tree.hh"

// extern Tree tree;
// extern LBNPool lbnPoolManager; 
// extern Mapping mappingManager;


namespace SimpleSSD {

namespace HIL {

namespace NVMe {

Namespace::Namespace(Subsystem *p, ConfigData &c)
    : pParent(p),
      pDisk(nullptr),
      cfgdata(c),
      conf(*c.pConfigReader),
      nsid(NSID_NONE),
      attached(false),
      allocated(false),
      formatFinishedAt(0) {}

Namespace::~Namespace() {
  if (pDisk) {
    delete pDisk;
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
        // case OPCODE_MONITOR_IMS:
        //   debugprint(LOG_IMS,
        //              "IMS     | Monitor IMS | SQ %u:%u | CID %u | NSID %-5d",
        //              req.sqID, req.sqUID, req.entry.dword0.commandID, nsid);
        //   monitor_IMS(req, func);
        //   break;
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

  if (!attached) {
    err = true;
    resp.makeStatus(true, false, TYPE_COMMAND_SPECIFIC_STATUS,
                    STATUS_NAMESPACE_NOT_ATTACHED);
  }
  uint64_t lbn = INVALIDLBN;
  err = ims.write_sstable(lbn);
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

        if (pContext->beginAt == 2) {
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

        if (pContext->beginAt == 2) {
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
              LOG_HIL_NVME,
              "WRITE_BUFFER is done");

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
          debugprint(LOG_HIL_NVME,"READ_BUFFER is done");

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


// void Namespace::search_key(SQEntryWrapper &req, RequestFunction &func) {
//   bool err = false;
//   uint32_t numberOfSize = req.entry.dword12;
//   CQEntryWrapper resp(req);

//   if (!attached) {
//     err = true;
//     resp.makeStatus(true, false, TYPE_COMMAND_SPECIFIC_STATUS,
//                     STATUS_NAMESPACE_NOT_ATTACHED);
//   }
//   // if (nlb == 0) {
//   //   err = true;
//   //   warn("nvme_namespace: host tried to write 0 blocks");
//   // }
  
//   if (!err) {
//     DMAFunction doRead = [this](uint64_t tick, void *context) {
//       DMAFunction dmaDone = [this](uint64_t tick, void *context) {
//         IOContext *pContext = (IOContext *)context;

//         pContext->beginAt++;

//         if (pContext->beginAt == 2) {
//           debugprint(
//             LOG_IMS,
//             "NVM     | search_key | CQ %u | SQ %u:%u | CID %u | NSID %-5d | "
//             "LBN %ld | LPN %ld + %ld | %" PRIu64 " - %" PRIu64 " (%" PRIu64 ")",
//             pContext->resp.cqID,
//             pContext->resp.entry.dword2.sqID,
//             pContext->resp.sqUID,
//             pContext->resp.entry.dword3.commandID,
//             nsid,
//             pContext->lbn,
//             pContext->lpn,
//             pContext->nlpn,
//             pContext->tick,
//             tick,
//             tick - pContext->tick);

//           pContext->function(pContext->resp);
//           pParent->writeIMS(this, pContext->lpn, pContext->nlpn, dmaDone, context);
//           if (pContext->buffer) {
//             // pDisk->writeBlock(pContext->lbn,pContext->buffer);

//             free(pContext->buffer);
//           }

//           delete pContext->dma;
//           delete pContext;
//         }
//       };

//       IOContext *pContext = (IOContext *)context;

//       pContext->tick = tick;
//       pContext->beginAt = 0;

//       if (pDisk) {
//         pContext->buffer = (uint8_t *)calloc(pContext->nlb, 1);

//         pContext->dma->read(0, (uint64_t)pContext->nlb, pContext->buffer,
//                             dmaDone, context);
//       }
//       else {
//         pContext->dma->read(0, (uint64_t)pContext->nlb, nullptr, dmaDone,
//                             context);
//       }
//       // pParent->writeIMS(this, pContext->lpn, pContext->nlpn, dmaDone, context);
//     };

//     IOContext *pContext = new IOContext(func, resp);

//     pContext->beginAt = getTick();
//     pContext->nlb     = numberOfSize;
//     // Using nlb to transfer the variable of my data size
//     CPUContext *pCPU =
//         new CPUContext(doRead, pContext, CPU::NVME__NAMESPACE, CPU::READ);

//     if (req.useSGL) {
//       pContext->dma =
//           new SGL(cfgdata, cpuHandler, pCPU, req.entry.data1, req.entry.data2);
//     }
//     else {
//       pContext->dma =
//           new PRPList(cfgdata, cpuHandler, pCPU, req.entry.data1,
//                       req.entry.data2, (uint64_t)numberOfSize);
//     }
//   }
//   else {
//     func(resp);
//   }
// }


void Namespace::search_key(SQEntryWrapper &req, RequestFunction &func) {
  bool err = false;

  CQEntryWrapper resp(req);

  // host 傳進來的 data size（你前面就有用 dword12 表示）
  uint32_t numberOfSize = req.entry.dword12;

  // 1. 基本檢查：Namespace 是否有 attach
  if (!attached) {
    err = true;
    resp.makeStatus(true, false, TYPE_COMMAND_SPECIFIC_STATUS,
                    STATUS_NAMESPACE_NOT_ATTACHED);
  }

  // 2. 如果前面就錯了，直接回 CQ
  if (err) {
    debugprint(LOG_IMS,
               "NVM     | SEARCH_KEY | Command failed (namespace not attached)");
    func(resp);
    return;
  }
  if (!err) {
    DMAFunction doRead = [this](uint64_t tick, void *context) {
      DMAFunction dmaDone = [this](uint64_t tick, void *context) {
        DMAFunction searchDone = [this](uint64_t tick, void *context){
          IOContext *pContext = (IOContext *)context;
          pContext->beginAt++;
          
          if(pContext->beginAt == pContext->nlb){
            pContext->resp.makeStatus(false, false,
                                      TYPE_GENERIC_COMMAND_STATUS,
                                      STATUS_SUCCESS);
            pContext->function(pContext->resp);
            
            // 清理 buffer / DMA / context
            if (pContext->buffer) {
              free(pContext->buffer);
              pContext->buffer = nullptr;
            }

            if (pContext->dma) {
              delete pContext->dma;
              pContext->dma = nullptr;
            }

            delete pContext;
          }
        };
        IOContext *pContext = (IOContext *)context;
        std::vector<uint64_t> lbn_list;

        int ret = ims.search(lbn_list);
        bool searchErr = (ret == OPERATION_FAILURE);
        
        if (searchErr) {
          debugprint(LOG_IMS,
                      "NVM     | SEARCH_KEY | ims.search() failed");
          pContext->resp.makeStatus(true, false,
                                    TYPE_COMMAND_SPECIFIC_STATUS,
                                    STATUS_COMMAND_FAILD);
          pContext->function(pContext->resp);
          // 清理 buffer / DMA / context
          if (pContext->buffer) {
            free(pContext->buffer);
            pContext->buffer = nullptr;
          }

          if (pContext->dma) {
            delete pContext->dma;
            pContext->dma = nullptr;
          }

          delete pContext;
        }
        else if(lbn_list.empty()){
          debugprint(LOG_IMS,
                      "NVM     | SEARCH_KEY | ims.search() lbn_list is empty");
          pContext->resp.makeStatus(false, false,
                                      TYPE_GENERIC_COMMAND_STATUS,
                                      STATUS_SUCCESS);
          pContext->function(pContext->resp);
          // 清理 buffer / DMA / context
          if (pContext->buffer) {
            free(pContext->buffer);
            pContext->buffer = nullptr;
          }

          if (pContext->dma) {
            delete pContext->dma;
            pContext->dma = nullptr;
          }

          delete pContext;
        } 
        else {
          // record how mant time searchDone will be execute 
          pContext->nlb = lbn_list.size();
          debugprint(LOG_IMS,
                      "NVM     | SEARCH_KEY | ims.search() success");
          for(auto lbn : lbn_list){
            uint64_t slpn = LBN2LPN(lbn);
            uint64_t nlpn = 1;
            pParent->readIMSDirectFTL(this, slpn, nlpn, searchDone, pContext);
          } 
        }
      };

      IOContext *pContext = (IOContext *)context;

      pContext->tick = tick;
      pContext->beginAt = 0;

      if (pContext->nlb == 0) {
        dmaDone(tick, context);
        return;
      }

      pContext->buffer = (uint8_t *)calloc(pContext->nlb, sizeof(uint8_t));

      pContext->dma->read(0, (uint64_t)pContext->nlb, pContext->buffer,
                          dmaDone, context);
    };

    IOContext *pContext = new IOContext(func, resp);

    pContext->beginAt = getTick();
    pContext->nlb = numberOfSize;

    CPUContext *pCPU =
        new CPUContext(doRead, pContext, CPU::NVME__NAMESPACE, CPU::READ);

    if (req.useSGL) {
      pContext->dma =
          new SGL(cfgdata, cpuHandler, pCPU, req.entry.data1, req.entry.data2);
    } else {
      pContext->dma =
          new PRPList(cfgdata, cpuHandler, pCPU, req.entry.data1,
                      req.entry.data2, (uint64_t)numberOfSize);
    }
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
    int ret = ims.erase_sstable(lbn);   // 這裡會順便更新 IMS 的 mapping / LBNPool / Tree

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

  // ====== 下面這段就是「更新 FTL，這個 block 要 erase」的關鍵 ======

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

    // 回報給 host
    pContext->function(pContext->resp);

    delete pContext;
  };

  // IOContext 用來帶 resp + 完成 callback
  IOContext *pContext = new IOContext(func, resp);
  pContext->beginAt = getTick();
  pContext->lbn     = lbn;
  pContext->lpn     = slpn;
  pContext->nlpn    = nlpn;

  // 不需要 DMA，直接用 LPN range 叫 Subsystem 去做 trimIMS
  pParent->trimIMS(this, slpn, nlpn, doTrimDone, pContext);
}

void Namespace::compaction_io(SQEntryWrapper &req, RequestFunction &func) {
    CQEntryWrapper resp(req);

    // 1. 檢查 namespace 是否 attached
    if (!attached) {
        resp.makeStatus(true, false,
                        TYPE_COMMAND_SPECIFIC_STATUS,
                        STATUS_NAMESPACE_NOT_ATTACHED);
        debugprint(LOG_IMS,
                   "NVM     | Compaction IO sim | Command failed (namespace not attached)");
        func(resp);
        return;
    }

    // 2. 建立 IOContext ＋ 讓 IMS 算出這次要 fake 的 LBN 列表
    IOContext *pContext = new IOContext(func, resp);
    std::vector<uint64_t> lbn_list;

    int ret = ims.simulate_compaction_io(lbn_list);

    if (ret == OPERATION_FAILURE) {
        debugprint(LOG_IMS,
                   "NVM     | Compaction IO sim | ims.simulate_compaction_io() failed");
        pContext->resp.makeStatus(true, false,
                                  TYPE_COMMAND_SPECIFIC_STATUS,
                                  STATUS_COMMAND_FAILD);
        pContext->function(pContext->resp);
        delete pContext;
        return;   // 一定要 return
    }

    if (lbn_list.empty()) {
        debugprint(LOG_IMS,
                   "NVM     | Compaction IO sim | lbn_list is empty");
        pContext->resp.makeStatus(false, false,
                                  TYPE_GENERIC_COMMAND_STATUS,
                                  STATUS_SUCCESS);
        pContext->function(pContext->resp);
        if (pContext->dma) {
            delete pContext->dma;
            pContext->dma = nullptr;
        }
        delete pContext;
        return;   // 一定要 return
    }

    // 3. 正式丟 fake read 到 FTL，完全不做 DMA 回 host
    pContext->beginAt = 0;
    pContext->nlb     = lbn_list.size();

    DMAFunction done = [this](uint64_t tick, void *context) {
        IOContext *pContext = static_cast<IOContext*>(context);
        pContext->beginAt++;

        if (pContext->beginAt == pContext->nlb) {
            pContext->resp.makeStatus(false, false,
                                      TYPE_GENERIC_COMMAND_STATUS,
                                      STATUS_SUCCESS);
            pContext->function(pContext->resp);

            if (pContext->buffer) {
                free(pContext->buffer);
                pContext->buffer = nullptr;
            }
            if (pContext->dma) {
                delete pContext->dma;
                pContext->dma = nullptr;
            }
            delete pContext;
        }
    };

    debugprint(LOG_IMS,
               "NVM     | Compaction IO sim | ims.simulate_compaction_io() success, nlb=%zu",
               lbn_list.size());

    for (auto lbn : lbn_list) {
        uint64_t slpn = LBN2LPN(lbn);
        uint64_t nlpn = IMS_PAGE_NUM;
        pParent->readIMSDirectFTL(this, slpn, nlpn, done, pContext);
    }
}


}  // namespace NVMe

}  // namespace HIL

}  // namespace SimpleSSD
