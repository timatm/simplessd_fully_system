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

#include "util/disk.hh"
#include "ims/firmware/def.hh"
#include "ims/firmware/print.hh"
#include <cstring>


#ifdef _MSC_VER
#include <Windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace SimpleSSD {

Disk::Disk() : diskSize(0), sectorSize(0) {}

Disk::~Disk() {
  close();
}

uint64_t Disk::open(std::string path, uint64_t desiredSize, uint32_t lbaSize) {
  filename = path;
  sectorSize = lbaSize;

  // Validate size
#ifdef _MSC_VER
  LARGE_INTEGER size;
  HANDLE hFile = CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE, NULL,
                             NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

  if (GetLastError() == ERROR_ALREADY_EXISTS) {
    if (GetFileSizeEx(hFile, &size)) {
      diskSize = size.QuadPart;
    }
    else {
      // panic("Get file size failed!");
    }
  }
  else {
    size.QuadPart = desiredSize;

    if (SetFilePointerEx(hFile, size, nullptr, FILE_BEGIN)) {
      if (SetEndOfFile(hFile)) {
        diskSize = desiredSize;
      }
      else {
        // panic("SetEndOfFile failed");
      }
    }
    else {
      // panic("SetFilePointerEx failed");
    }
  }

  CloseHandle(hFile);
#else
  struct stat s;

  if (stat(filename.c_str(), &s) == 0) {
    // File exists
    if (S_ISREG(s.st_mode)) {
      diskSize = s.st_size;
    }
    else {
      // panic("nvme_disk: Specified file %s is not regular file.\n",
      //       filename.c_str());
    }
  }
  else {
    // Create zero-sized file
    disk.open(filename, std::ios::out | std::ios::binary);
    disk.close();

    // Set file size
    if (truncate(filename.c_str(), desiredSize) == 0) {
      diskSize = desiredSize;
    }
    else {
      // panic("nvme_disk: Failed to set disk size %" PRIu64 " errno=%d\n",
      //       diskSize, errno);
    }
  }
#endif

  // Open file
  disk.open(filename, std::ios::in | std::ios::out | std::ios::binary);

  if (!disk.is_open()) {
    // panic("failed to open file");
  }

  return diskSize;
}

void Disk::close() {
  if (disk.is_open()) {
    disk.close();
  }
}

uint16_t Disk::read(uint64_t slba, uint16_t nlblk, uint8_t *buffer) {
  uint16_t ret = 0;

  if (disk.is_open()) {
    uint64_t avail;

    slba *= sectorSize;
    avail = nlblk * sectorSize;

    if (slba + avail > diskSize) {
      if (slba >= diskSize) {
        avail = 0;
      }
      else {
        avail = diskSize - slba;
      }
    }

    if (avail > 0) {
      disk.seekg(slba, std::ios::beg);
      if (!disk.good()) {
        // panic("nvme_disk: Fail to seek to %" PRIu64 "\n", slba);
      }

      disk.read((char *)buffer, avail);
    }

    memset(buffer + avail, 0, nlblk * sectorSize - avail);

    // DPRINTF(NVMeDisk, "DISK    | READ  | BYTE %016" PRIX64 " + %X\n",
    //         slba, nlblk * sectorSize);

    ret = nlblk;
  }

  return ret;
}

uint16_t Disk::write(uint64_t slba, uint16_t nlblk, uint8_t *buffer) {
  uint16_t ret = 0;

  if (disk.is_open()) {
    slba *= sectorSize;

    disk.seekp(slba, std::ios::beg);
    if (!disk.good()) {
      // panic("nvme_disk: Fail to seek to %" PRIu64 "\n", slba);
    }

    uint64_t offset = disk.tellp();
    disk.write((char *)buffer, sectorSize * nlblk);
    offset = (uint64_t)disk.tellp() - offset;

    // DPRINTF(NVMeDisk, "DISK    | WRITE | BYTE %016" PRIX64 " + %X\n", slba,
    //         offset);

    ret = offset / sectorSize;
  }

  return ret;
}

uint16_t Disk::erase(uint64_t, uint16_t nlblk) {
  return nlblk;
}

// Custom disk operations implementation

// uint16_t Disk::write_page(diskRequest request, uint8_t *buffer) {
//   uint16_t ret = 0;
//   if (disk.is_open()) {
//     uint64_t blockOffset = request.pageSize * request.totalPageNum;
//     uint64_t dieOffset = blockOffset * request.totalBlockNum;
//     uint64_t wayOffset = dieOffset * request.totalDieNum;
//     uint64_t planeOffset = wayOffset * request.totalWayNum;
//     uint64_t chOffset = planeOffset * request.totalPlaneNum;
//     uint64_t fileOffset = ((uint64_t)request.ch     * chOffset) +
//                           ((uint64_t)request.plane  * planeOffset) +
//                           ((uint64_t)request.way    * wayOffset) +
//                           ((uint64_t)request.die    * dieOffset) +
//                           ((uint64_t)request.block  * blockOffset) +
//                           ((uint64_t)request.page   * request.pageSize);

//     uint64_t diskSize = request.pageSize * request.totalPageNum * request.totalBlockNum * request.totalWayNum 
//                         * request.totalPlaneNum * request.totalChNum;

//     disk.seekp(fileOffset, std::ios::beg);
//     if (!disk.good()) {
//       // panic("nvme_disk: Fail to seek to %" PRIu64 "\n", slba);
//     }

//     if (fileOffset + request.pageSize > diskSize) {
//       if (fileOffset >= diskSize) {
//         debugprint(LOG_IMS, "DISK    | write_page | write pointer out of bound");
//         return 0;
//       } else {
//         // can't write whole page
//         debugprint(LOG_IMS, "DISK    | write_page | partial write blocked");
//         return 0;
//       }
//     }

//     uint64_t offset = disk.tellp();
//     disk.write((char *)buffer, request.pageSize);
//     offset = (uint64_t)disk.tellp() - offset;
//     debugprint(LOG_IMS,
//       "DISK    | write_page | CH: %d | PLANE: %d | WAY: %d | DIE: %d | BLOCK: %d | PAGE: %d",request.ch,request.plane,request.way,request.die,request.block,request.page);
//     debugprint(LOG_IMS,
//                "DISK    | write_page | BYTE %016" PRIX64 " + %X\n", fileOffset, offset);
//     // DPRINTF(NVMeDisk, "DISK    | WRITE | BYTE %016" PRIX64 " + %X\n", slba,
//     //         offset);

//     ret = offset;
//   }

//   return ret;
// }

// uint16_t Disk::write_sstable(diskRequest request,uint8_t *buffer){
//   uint16_t ret = 0;
//   if (disk.is_open()) {
//     for(uint32_t i = 0;i < request.totalPageNum;i++){
//       diskRequest pageReq = request;
//       uint8_t *page_ptr = buffer + i * request.pageSize;
//       pageReq.page = i;
//       debugprint(LOG_IMS,
//         "DISK    | write_sstable | CH: %d | PLANE: %d | WAY: %d | DIE: %d | BLOCK: %d | PAGE: %d",request.ch,request.plane,request.way,request.die,request.block,request.page);
//       uint16_t written = write_page(pageReq, page_ptr);
//       if (written == 0) {
//         debugprint(LOG_IMS, "DISK    | write_sstable | page %d write failed", i);
//         break;
//       }
//       ret += written;
//     }
//   }

//   return ret;
// }

// uint16_t Disk::read_page(diskRequest request,uint8_t *buffer){
//   uint16_t ret = 0;
//   uint32_t readSize = request.pageSize;
//   if (disk.is_open()) {
//     uint64_t blockOffset = request.pageSize * request.totalPageNum;
//     uint64_t dieOffset = blockOffset * request.totalBlockNum;
//     uint64_t wayOffset = dieOffset * request.totalDieNum;
//     uint64_t planeOffset = wayOffset * request.totalWayNum;
//     uint64_t chOffset = planeOffset * request.totalPlaneNum;
//     uint64_t fileOffset = ((uint64_t)request.ch     * chOffset) +
//                           ((uint64_t)request.plane  * planeOffset) +
//                           ((uint64_t)request.way    * wayOffset) +
//                           ((uint64_t)request.die    * dieOffset) +
//                           ((uint64_t)request.block  * blockOffset) +
//                           ((uint64_t)request.page   * request.pageSize);

//     uint64_t diskSize = request.pageSize * request.totalPageNum * request.totalBlockNum * request.totalWayNum 
//                         * request.totalPlaneNum * request.totalChNum;
//     // 邊界檢查（避免越界）
//     if (fileOffset + readSize > diskSize) {
//       // 調整為只讀得下的部分，或完全不讀
//       if (fileOffset >= diskSize) {
//         debugprint(LOG_IMS,
//         "DISK    | read_page | read pointer is out of bound");
//         return 0;
//       } else {
//         readSize = diskSize - fileOffset;
//       }
//     }

//     disk.seekg(fileOffset, std::ios::beg);
//     if (!disk.good()) {
//       // panic("nvme_disk: Seek failed at offset %" PRIu64 "\n", offset);
//       return 0;
//     }

//     disk.read((char *)buffer, readSize);

//     std::streamsize bytesRead = disk.gcount();
//     if (bytesRead < request.pageSize) {
//       memset(buffer + bytesRead, 0, request.pageSize - bytesRead);
//     }

//     ret = 1;
//   }

//   return ret;
// }

// uint16_t Disk::read_sstable(diskRequest request, uint8_t *buffer) {
//   uint16_t ret = 0;

//   if (disk.is_open()) {
//     for (uint32_t i = 0; i < request.totalPageNum; i++) {
//       diskRequest pageReq = request;
//       pageReq.page = i;

//       uint8_t *page_ptr = buffer + i * request.pageSize;

//       debugprint(LOG_IMS,
//         "DISK    | read_sstable | CH: %d | PLANE: %d | WAY: %d | DIE: %d | BLOCK: %d | PAGE: %d",
//         pageReq.ch, pageReq.plane, pageReq.way, pageReq.die, pageReq.block, pageReq.page);

//       uint16_t read_result = read_page(pageReq, page_ptr);
//       if (read_result == 0) {
//         debugprint(LOG_IMS, "DISK    | read_sstable | page %d read failed", i);
//         break;
//       }
//       ret += read_result;
//     }
//   }

//   return ret;
// }

uint16_t Disk::readPage(uint64_t lpn, uint8_t *buffer) {
    if (!disk.is_open()) {
        throw std::runtime_error("Disk not opened.");
    }

    uint64_t offset = lpn * IMS_PAGE_SIZE;

    // 移動到指定 offset
    disk.seekg(offset, std::ios::beg);
    if (!disk.good()) {
        throw std::runtime_error("Seek failed.");
    }

    // 讀取資料
    disk.read(reinterpret_cast<char *>(buffer), IMS_PAGE_SIZE);
    if (!disk.good()) {
        throw std::runtime_error("Read error or EOF.");
    }

    return 1;  // 讀取成功 1 page
}


uint16_t Disk::writePage(uint64_t lpn,uint8_t *buffer) {
    if (!disk.is_open()) {
        throw std::runtime_error("Disk not opened.");
    }

    uint64_t offset = lpn * IMS_PAGE_SIZE;

    // 移動到對應位置
    disk.seekp(offset, std::ios::beg);
    if (!disk.good()) {
        throw std::runtime_error("Seek failed.");
    }

    // 寫入資料
    disk.write(reinterpret_cast<const char *>(buffer), IMS_PAGE_SIZE);
    if (!disk.good()) {
        throw std::runtime_error("Write failed.");
    }

    // 確保資料刷新到磁碟
    disk.flush();
    if (!disk.good()) {
        throw std::runtime_error("Flush failed.");
    }

    return 1;  // 寫入了一個 page
}


uint16_t Disk::writeBlock(uint64_t lbn, uint8_t *buffer) {
    if (!disk.is_open()) {
        throw std::runtime_error("Disk not opened.");
    }

    uint16_t ret = 0;
    uint64_t baseLpn = LBN2LPN(lbn);

    for (int i = 0; i < IMS_PAGE_NUM; i++) {
        uint64_t lpn = baseLpn + i;
        uint8_t *page_ptr = buffer + i * IMS_PAGE_SIZE;

        uint16_t written = writePage(lpn, page_ptr);
        if (written == 0) {
            pr_info("[ERROR] write block failed at page: %d LPN: %lu", i, lpn);
            break;
        }
        ret += written;
    }

    return ret;
}


uint16_t Disk::readBlock(uint64_t lbn, uint8_t *buffer) {
    if (!disk.is_open()) {
        throw std::runtime_error("Disk not opened.");
    }

    uint16_t ret = 0;
    uint64_t baseLpn = LBN2LPN(lbn);

    for (int i = 0; i < IMS_PAGE_NUM; i++) {
        uint64_t lpn = baseLpn + i;
        uint8_t *page_ptr = buffer + i * IMS_PAGE_SIZE;

        uint16_t read_result = readPage(lpn, page_ptr);
        if (read_result == 0) {
            pr_info("[ERROR] read block failed at page: %d LPN: %lu", i, lpn);
            break;
        }
        ret += read_result;
    }

    return ret;
}


CoWDisk::CoWDisk() {}

CoWDisk::~CoWDisk() {
  close();
}

void CoWDisk::close() {
  table.clear();

  Disk::close();
}

uint16_t CoWDisk::read(uint64_t slba, uint16_t nlblk, uint8_t *buffer) {
  uint16_t read = 0;

  for (uint64_t i = 0; i < nlblk; i++) {
    auto block = table.find(slba + i);

    if (block != table.end()) {
      memcpy(buffer + i * sectorSize, block->second.data(), sectorSize);
      read++;
    }
    else {
      read += Disk::read(slba + i, 1, buffer + i * sectorSize);
    }
  }

  return read;
}

uint16_t CoWDisk::write(uint64_t slba, uint16_t nlblk, uint8_t *buffer) {
  uint16_t write = 0;

  for (uint64_t i = 0; i < nlblk; i++) {
    auto block = table.find(slba + i);

    if (block != table.end()) {
      memcpy(block->second.data(), buffer + i * sectorSize, sectorSize);
    }
    else {
      std::vector<uint8_t> data;

      data.resize(sectorSize);
      memcpy(data.data(), buffer + i * sectorSize, sectorSize);

      table.insert({slba + i, data});
    }

    write++;
  }

  return write;
}

uint64_t MemDisk::open(std::string, uint64_t size, uint32_t lbaSize) {
  diskSize = size;
  sectorSize = lbaSize;

  return size;
}

void MemDisk::close() {
  table.clear();
}

MemDisk::MemDisk() {}

MemDisk::~MemDisk() {
  close();
}

uint16_t MemDisk::read(uint64_t slba, uint16_t nlblk, uint8_t *buffer) {
  uint16_t read = 0;

  for (uint64_t i = 0; i < nlblk; i++) {
    auto block = table.find(slba + i);

    if (block != table.end()) {
      memcpy(buffer + i * sectorSize, block->second.data(), sectorSize);
    }
    else {
      memset(buffer + i * sectorSize, 0, sectorSize);
    }

    read++;
  }

  return read;
}

uint16_t MemDisk::write(uint64_t slba, uint16_t nlblk, uint8_t *buffer) {
  uint16_t write = 0;

  for (uint64_t i = 0; i < nlblk; i++) {
    auto block = table.find(slba + i);

    if (block != table.end()) {
      memcpy(block->second.data(), buffer + i * sectorSize, sectorSize);
    }
    else {
      std::vector<uint8_t> data;

      data.resize(sectorSize);
      memcpy(data.data(), buffer + i * sectorSize, sectorSize);

      table.insert({slba + i, data});
    }

    write++;
  }

  return write;
}
uint16_t MemDisk::erase(uint64_t slba, uint16_t nlblk) {
  uint16_t erase = 0;

  for (uint64_t i = 0; i < nlblk; i++) {
    auto block = table.find(slba + i);

    if (block != table.end()) {
      table.erase(block);
    }

    erase++;
  }

  return erase;
}



}  // namespace SimpleSSD
