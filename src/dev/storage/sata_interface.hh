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

#ifndef __DEV_STORAGE_NVME_INTERFACE_HH__
#define __DEV_STORAGE_NVME_INTERFACE_HH__

#include <cinttypes>
#include <queue>

#include "dev/io_device.hh"
#include "dev/pci/device.hh"
#include "dev/storage/def.hh"
#include "dev/storage/simplessd/hil/sata/interface.hh"
#include "dev/storage/simplessd/sim/config_reader.hh"
#include "dev/storage/simplessd/sim/simulator.hh"
#include "params/SATAInterface.hh"

class SATAInterface : public PciDevice,
                      public SimpleSSD::HIL::SATA::Interface,
                      public EventEngine {
 private:
  std::string configPath;
  SimpleSSD::ConfigReader conf;

  // PCI Express
  SimpleSSD::PCIExpress::PCIE_GEN pcieGen;
  uint8_t pcieLane;

  // BAR addresses
  uint32_t AHCIRegisters;

  /* Interrupt logics */
  // Pin based
  uint32_t IS;
  uint32_t ISold;

  // MSI/MSI-X
  uint16_t vectors;

  // Current Interrupt Mode
  INTERRUPT_MODE mode;

  // DMA scheduling
  EventFunctionWrapper dmaReadEvent;
  EventFunctionWrapper dmaWriteEvent;
  std::queue<DMAEntry> dmaReadQueue;
  std::queue<DMAEntry> dmaWriteQueue;
  bool dmaReadPending;
  bool dmaWritePending;

  // Stats
  EventFunctionWrapper statUpdateEvent;
  Stats::Scalar *pStats;

  void writeInterrupt(Addr, size_t, uint8_t *);
  void dmaReadDone();
  void submitDMARead();
  void dmaWriteDone();
  void submitDMAWrite();

 public:
  typedef SATAInterfaceParams Params;
  const Params *params() const { return (const Params *)_params; }
  SATAInterface(Params *p);
  ~SATAInterface();

  Tick writeConfig(PacketPtr pkt) override;
  Tick readConfig(PacketPtr pkt) override;

  Tick read(PacketPtr pkt) override;
  Tick write(PacketPtr pkt) override;

  void serialize(CheckpointOut &cp) const override;
  void unserialize(CheckpointIn &cp) override;

  void regStats() override;
  void resetStats() override;
  void updateStats();

  // Interface <-> Controller
  void dmaRead(uint64_t, uint64_t, uint8_t *, SimpleSSD::DMAFunction &,
               void * = nullptr) override;
  void dmaWrite(uint64_t, uint64_t, uint8_t *, SimpleSSD::DMAFunction &,
                void * = nullptr) override;
  void updateInterrupt(bool) override;
};

#endif
