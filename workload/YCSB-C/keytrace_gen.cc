// Compile commamd
// g++ -O3 -std=c++17 -Wall -Wextra -Wno-unused-parameter -pthread -I. -I./core -o keytrace_gen keytrace_gen.cc
// How to use
// ./keytrace_gen --dist zipfian --recordcount 10000000 --operationcount 5000000 --out zipfian.ktrc
// ./keytrace_gen --dist latest  --recordcount 10000000 --operationcount 5000000 --out latest.ktrc



#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>

// Use your project's header layout. In the upstream YCSB-C tree these live under "core/".
#include "core/scrambled_zipfian_generator.h"
#include "core/zipfian_generator.h"
#include "core/counter_generator.h"

namespace {

struct Header {
  char magic[4];
  uint32_t version;
  uint32_t reserved;
  uint64_t nkeys;
};

static inline std::string Trim(std::string s) {
  auto not_space = [](unsigned char ch) { return !std::isspace(ch); };
  s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
  s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
  return s;
}

static std::unordered_map<std::string, std::string> ParsePropertiesFile(const std::string &path) {
  std::unordered_map<std::string, std::string> props;
  std::ifstream in(path);
  if (!in.is_open()) {
    throw std::runtime_error("Cannot open properties fileworkload/YCSB-C/keytrace_gen: " + path);
  }
  std::string line;
  while (std::getline(in, line)) {
    line = Trim(line);
    if (line.empty()) continue;
    if (line[0] == '#') continue;

    // Strip inline comments: key=value # comment
    auto hash = line.find('#');
    if (hash != std::string::npos) {
      line = Trim(line.substr(0, hash));
      if (line.empty()) continue;
    }

    // Accept key=value or key: value
    std::size_t pos = line.find('=');
    if (pos == std::string::npos) pos = line.find(':');
    if (pos == std::string::npos) continue;

    std::string key = Trim(line.substr(0, pos));
    std::string val = Trim(line.substr(pos + 1));
    if (!key.empty()) {
      props[key] = val;
    }
  }
  return props;
}

static uint64_t GetU64(const std::unordered_map<std::string, std::string> &p,
                       const std::string &key,
                       uint64_t defv) {
  auto it = p.find(key);
  if (it == p.end() || it->second.empty()) return defv;
  return static_cast<uint64_t>(std::stoull(it->second));
}

static double GetDouble(const std::unordered_map<std::string, std::string> &p,
                        const std::string &key,
                        double defv) {
  auto it = p.find(key);
  if (it == p.end() || it->second.empty()) return defv;
  return std::stod(it->second);
}

static std::string GetStr(const std::unordered_map<std::string, std::string> &p,
                          const std::string &key,
                          const std::string &defv) {
  auto it = p.find(key);
  if (it == p.end()) return defv;
  return it->second;
}

static void Usage(const char *argv0) {
  std::cerr
      << "Usage: " << argv0 << " [options]\n\n"
      << "Options:\n"
      << "  -P <file>                 YCSB property file (optional)\n"
      << "  --dist <zipfian|latest>    Distribution mode (required)\n"
      << "  --out <file>               Output trace file (required)\n"
      << "  --format <bin|txt>         Output format (default: bin)\n"
      << "  --recordcount <N>          Override recordcount\n"
      << "  --operationcount <N>       Override operationcount\n"
      << "  --insertstart <N>          Override insertstart (default: from prop or 0)\n"
      << "  --theta <double>           Zipfian constant theta (default: 0.99)\n";
}

// "latest" distribution generator (SkewedLatest) built from Zipfian.
// It biases toward the most recently inserted keys.
class LatestGenerator {
 public:
  LatestGenerator(uint64_t insertstart, uint64_t recordcount, double theta)
      : base_(insertstart),
        num_(recordcount),
        // CounterGenerator::Last() returns counter_-1.
        // Set counter_ to (insertstart + recordcount) so last == insertstart + recordcount - 1.
        basis_(insertstart + recordcount),
        zipf_(0, recordcount - 1, theta),
        last_(0) {
    if (recordcount < 2) {
      throw std::runtime_error("recordcount must be >= 2 for latest distribution");
    }
  }

  uint64_t Next() {
    const uint64_t max_key = basis_.Last();          // insertstart + recordcount - 1
    const uint64_t nitems = num_;                    // recordcount
    uint64_t off = zipf_.Next(nitems);               // expected in [0, nitems-1]
    if (off >= nitems) off = nitems - 1;            // safety clamp
    last_ = max_key - off;
    return last_;
  }

  uint64_t Last() const { return last_; }

 private:
  uint64_t base_;
  uint64_t num_;
  ycsbc::CounterGenerator basis_;
  ycsbc::ZipfianGenerator zipf_;
  uint64_t last_;
};

} // namespace

int main(int argc, char **argv) {
  try {
    std::string prop_file;
    std::string dist;
    std::string out_path;
    std::string format = "bin";

    uint64_t recordcount = 0;
    uint64_t operationcount = 0;
    uint64_t insertstart = 0;
    double theta = ycsbc::ZipfianGenerator::kZipfianConst;

    // Parse args
    for (int i = 1; i < argc; ++i) {
      std::string a = argv[i];
      if (a == "-P" && i + 1 < argc) {
        prop_file = argv[++i];
      } else if (a == "--dist" && i + 1 < argc) {
        dist = argv[++i];
      } else if ((a == "--out" || a == "-o") && i + 1 < argc) {
        out_path = argv[++i];
      } else if (a == "--format" && i + 1 < argc) {
        format = argv[++i];
      } else if (a == "--recordcount" && i + 1 < argc) {
        recordcount = static_cast<uint64_t>(std::stoull(argv[++i]));
      } else if (a == "--operationcount" && i + 1 < argc) {
        operationcount = static_cast<uint64_t>(std::stoull(argv[++i]));
      } else if (a == "--insertstart" && i + 1 < argc) {
        insertstart = static_cast<uint64_t>(std::stoull(argv[++i]));
      } else if (a == "--theta" && i + 1 < argc) {
        theta = std::stod(argv[++i]);
      } else if (a == "-h" || a == "--help") {
        Usage(argv[0]);
        return 0;
      } else {
        std::cerr << "Unknown/invalid option: " << a << "\n";
        Usage(argv[0]);
        return 1;
      }
    }

    // Load property file (if any)
    if (!prop_file.empty()) {
      auto props = ParsePropertiesFile(prop_file);
      // Standard YCSB keys
      if (recordcount == 0) recordcount = GetU64(props, "recordcount", 0);
      if (operationcount == 0) operationcount = GetU64(props, "operationcount", 0);
      if (insertstart == 0) insertstart = GetU64(props, "insertstart", 0);
      // Some workloads use "requestdistribution"; allow using it as default dist if not specified.
      if (dist.empty()) dist = GetStr(props, "requestdistribution", "");
      // Optional override for zipfian constant
      theta = GetDouble(props, "zipfianconstant", theta);
    }

    if (dist.empty() || out_path.empty() || recordcount == 0 || operationcount == 0) {
      std::cerr << "Missing required args.\n";
      Usage(argv[0]);
      return 1;
    }

    const uint64_t min_key = insertstart;
    const uint64_t max_key = insertstart + recordcount - 1;

    std::cerr << "[keytrace_gen] dist=" << dist
              << " recordcount=" << recordcount
              << " operationcount=" << operationcount
              << " insertstart=" << insertstart
              << " keyrange=[" << min_key << "," << max_key << "]"
              << " theta=" << theta
              << " format=" << format
              << " out=" << out_path << "\n";

    if (format == "bin") {
      std::ofstream out(out_path, std::ios::binary);
      if (!out.is_open()) throw std::runtime_error("Cannot open output: " + out_path);

      const char magic[4] = {'K','T','R','C'};
      uint32_t version = 1;
      uint32_t reserved = 0;
      uint64_t nkeys = operationcount;

      out.write(magic, 4);
      out.write(reinterpret_cast<const char*>(&version), sizeof(version));
      out.write(reinterpret_cast<const char*>(&reserved), sizeof(reserved));
      out.write(reinterpret_cast<const char*>(&nkeys), sizeof(nkeys));


      if (dist == "zipfian") {
        ycsbc::ScrambledZipfianGenerator gen(min_key, max_key, theta);
        for (uint64_t i = 0; i < operationcount; ++i) {
          uint64_t k = gen.Next();
          out.write(reinterpret_cast<const char *>(&k), sizeof(k));
        }
      } else if (dist == "latest") {
        LatestGenerator gen(min_key, recordcount, theta);
        for (uint64_t i = 0; i < operationcount; ++i) {
          uint64_t k = gen.Next();
          out.write(reinterpret_cast<const char *>(&k), sizeof(k));
        }
      } else {
        throw std::runtime_error("Unsupported dist: " + dist + " (only zipfian/latest)");
      }

      if (!out.good()) {
        throw std::runtime_error("Write failed (disk full?)");
      }

    } else if (format == "txt") {
      std::ofstream out(out_path);
      if (!out.is_open()) throw std::runtime_error("Cannot open output: " + out_path);

      if (dist == "zipfian") {
        ycsbc::ScrambledZipfianGenerator gen(min_key, max_key, theta);
        for (uint64_t i = 0; i < operationcount; ++i) {
          out << gen.Next() << "\n";
        }
      } else if (dist == "latest") {
        LatestGenerator gen(min_key, recordcount, theta);
        for (uint64_t i = 0; i < operationcount; ++i) {
          out << gen.Next() << "\n";
        }
      } else {
        throw std::runtime_error("Unsupported dist: " + dist + " (only zipfian/latest)");
      }

    } else {
      throw std::runtime_error("Unsupported format: " + format + " (use bin/txt)");
    }

    std::cerr << "[keytrace_gen] done\n";
    return 0;

  } catch (const std::exception &e) {
    std::cerr << "[keytrace_gen] ERROR: " << e.what() << "\n";
    return 1;
  }
}


