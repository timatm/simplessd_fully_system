#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

// 依你的專案 include 路徑調整：
// 如果你的檔案在 core/ 目錄，就用 "core/scrambled_zipfian_generator.h"
#include "scrambled_zipfian_generator.h"

int main(int argc, char** argv) {
  if (argc < 4) {
    std::cerr << "Usage: " << argv[0]
              << " <recordcount> <operationcount> <out_file> [insert_proportion]\n";
    return 1;
  }

  const uint64_t recordcount    = std::stoull(argv[1]);
  const uint64_t operationcount = std::stoull(argv[2]);
  const std::string out_path    = argv[3];
  const double insert_prop      = (argc >= 5) ? std::stod(argv[4]) : 0.0;

  // 對齊你 core_workload.cc 裡 zipfian 分支的邏輯（fudge factor）
  const uint64_t new_keys = static_cast<uint64_t>(operationcount * insert_prop * 2.0);

  // 注意：ScrambledZipfianGenerator 的 keyspace 是 recordcount+new_keys，
  // 但 ycsb NextTransactionKey() 會丟掉 > (insert_key_sequence_.Last()) 的 key，
  // 所以這裡也要做同樣的過濾，才能跟原行為一致。
  ycsbc::ScrambledZipfianGenerator gen(recordcount + new_keys);

  std::ofstream out(out_path);
  if (!out.is_open()) {
    std::cerr << "Cannot open output file: " << out_path << "\n";
    return 2;
  }

  uint64_t written = 0;
  while (written < operationcount) {
    uint64_t k = gen.Next();
    if (k < recordcount) {       // 對齊 NextTransactionKey() 的 do-while 過濾效果
      out << k << "\n";
      ++written;
    }
  }

  std::cerr << "Wrote " << written << " keys to " << out_path << "\n";
  return 0;
}


// g++ -O2 -std=c++11 keytrace_gen.cc -o keytrace_gen
// ./keytrace_gen 10000000 10000000 keytrace.txt 0.0
// # 例：recordcount=1e7, operationcount=1e7, insertproportion=0
