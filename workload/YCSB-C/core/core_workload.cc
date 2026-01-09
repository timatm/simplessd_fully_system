//
//  core_workload.cc
//  YCSB-C
//
//  Created by Jinglei Ren on 12/9/14.
//  Copyright (c) 2014 Jinglei Ren <jinglei@ren.systems>.
//

#include "uniform_generator.h"
#include "zipfian_generator.h"
#include "scrambled_zipfian_generator.h"
#include "skewed_latest_generator.h"
#include "const_generator.h"
#include "core_workload.h"
#include <fstream>
#include <atomic>
#include <vector>
#include <string>
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>


using ycsbc::CoreWorkload;
using std::string;

const string CoreWorkload::TABLENAME_PROPERTY = "table";
const string CoreWorkload::TABLENAME_DEFAULT = "usertable";

const string CoreWorkload::FIELD_COUNT_PROPERTY = "fieldcount";
const string CoreWorkload::FIELD_COUNT_DEFAULT = "10";

const string CoreWorkload::FIELD_LENGTH_DISTRIBUTION_PROPERTY =
    "field_len_dist";
const string CoreWorkload::FIELD_LENGTH_DISTRIBUTION_DEFAULT = "constant";

const string CoreWorkload::FIELD_LENGTH_PROPERTY = "fieldlength";
const string CoreWorkload::FIELD_LENGTH_DEFAULT = "100";

const string CoreWorkload::READ_ALL_FIELDS_PROPERTY = "readallfields";
const string CoreWorkload::READ_ALL_FIELDS_DEFAULT = "true";

const string CoreWorkload::WRITE_ALL_FIELDS_PROPERTY = "writeallfields";
const string CoreWorkload::WRITE_ALL_FIELDS_DEFAULT = "false";

const string CoreWorkload::READ_PROPORTION_PROPERTY = "readproportion";
const string CoreWorkload::READ_PROPORTION_DEFAULT = "0.95";

const string CoreWorkload::UPDATE_PROPORTION_PROPERTY = "updateproportion";
const string CoreWorkload::UPDATE_PROPORTION_DEFAULT = "0.05";

const string CoreWorkload::INSERT_PROPORTION_PROPERTY = "insertproportion";
const string CoreWorkload::INSERT_PROPORTION_DEFAULT = "0.0";

const string CoreWorkload::SCAN_PROPORTION_PROPERTY = "scanproportion";
const string CoreWorkload::SCAN_PROPORTION_DEFAULT = "0.0";

const string CoreWorkload::READMODIFYWRITE_PROPORTION_PROPERTY =
    "readmodifywriteproportion";
const string CoreWorkload::READMODIFYWRITE_PROPORTION_DEFAULT = "0.0";

const string CoreWorkload::REQUEST_DISTRIBUTION_PROPERTY =
    "requestdistribution";
const string CoreWorkload::REQUEST_DISTRIBUTION_DEFAULT = "uniform";

const string CoreWorkload::ZERO_PADDING_PROPERTY = "zeropadding";
const string CoreWorkload::ZERO_PADDING_DEFAULT = "1";

const string CoreWorkload::MAX_SCAN_LENGTH_PROPERTY = "maxscanlength";
const string CoreWorkload::MAX_SCAN_LENGTH_DEFAULT = "1000";

const string CoreWorkload::SCAN_LENGTH_DISTRIBUTION_PROPERTY =
    "scanlengthdistribution";
const string CoreWorkload::SCAN_LENGTH_DISTRIBUTION_DEFAULT = "uniform";

const string CoreWorkload::INSERT_ORDER_PROPERTY = "insertorder";
const string CoreWorkload::INSERT_ORDER_DEFAULT = "hashed";

const string CoreWorkload::INSERT_START_PROPERTY = "insertstart";
const string CoreWorkload::INSERT_START_DEFAULT = "0";

const string CoreWorkload::RECORD_COUNT_PROPERTY = "recordcount";
const string CoreWorkload::OPERATION_COUNT_PROPERTY = "operationcount";

const std::string CoreWorkload::KEY_TRACE_FILE_PROPERTY = "keytracefile";
const std::string CoreWorkload::KEY_TRACE_FILE_DEFAULT  = "";       // 空字串 = 不啟用
const std::string CoreWorkload::KEY_TRACE_LOOP_PROPERTY = "keytraceloop";
const std::string CoreWorkload::KEY_TRACE_LOOP_DEFAULT  = "false";


namespace ycsbc {

namespace {

static inline std::string Trim(std::string s) {
  auto not_space = [](unsigned char ch) { return !std::isspace(ch); };
  s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
  s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
  return s;
}

struct KtrcHeaderV1 {
  char magic[4];       // "KTRC"
  uint32_t version;    // 1
  uint32_t reserved;   // 0
  uint64_t nkeys;      // N
};

static inline void ReadFully(std::ifstream &in, void *buf, size_t bytes, const std::string &path) {
  char *p = reinterpret_cast<char *>(buf);
  size_t done = 0;
  while (done < bytes) {
    const size_t chunk = std::min(bytes - done, static_cast<size_t>(1 << 20)); // 1MB chunks
    in.read(p + done, static_cast<std::streamsize>(chunk));
    if (!in) {
      throw std::runtime_error("LoadKeyTraceFile: truncated/failed read: " + path);
    }
    done += chunk;
  }
}

class VectorTraceGenerator : public Generator<uint64_t> {
 public:
  VectorTraceGenerator(const std::vector<uint64_t>* keys, bool loop)
      : keys_(keys), loop_(loop) {
    if (!keys_ || keys_->empty()) {
      throw utils::Exception("VectorTraceGenerator: empty key trace");
    }
    last_.store((*keys_)[0], std::memory_order_relaxed);
  }

  uint64_t Next() override {
    uint64_t i = idx_.fetch_add(1, std::memory_order_relaxed);
    uint64_t v;
    if (loop_) {
      v = (*keys_)[i % keys_->size()];
    } else {
      v = (i >= keys_->size()) ? keys_->back() : (*keys_)[i];
    }
    last_.store(v, std::memory_order_relaxed);
    return v;
  }

  uint64_t Last() override {
    return last_.load(std::memory_order_relaxed);
  }

 private:
  const std::vector<uint64_t>* keys_;
  bool loop_;
  std::atomic<uint64_t> idx_{0};
  std::atomic<uint64_t> last_{0};
};

} // namespace

void CoreWorkload::LoadKeyTraceFile(const std::string &path) {
  key_trace_.clear();
  key_trace_pos_.store(0, std::memory_order_relaxed);

  // 先用 binary 模式打開
  std::ifstream in(path, std::ios::binary);
  if (!in.is_open()) {
    throw std::runtime_error("LoadKeyTraceFile: cannot open: " + path);
  }

  // 取得檔案大小
  in.seekg(0, std::ios::end);
  std::streamoff fsize = in.tellg();
  in.seekg(0, std::ios::beg);

  if (fsize < 4) {
    throw std::runtime_error("LoadKeyTraceFile: file too small: " + path);
  }

  // 讀 magic
  char magic[4] = {0, 0, 0, 0};
  in.read(magic, 4);
  if (!in) {
    throw std::runtime_error("LoadKeyTraceFile: cannot read header: " + path);
  }

  // ---- Case A: KTRC header ----
  if (std::memcmp(magic, "KTRC", 4) == 0) {
    uint32_t version = 0;
    uint32_t reserved = 0;
    uint64_t nkeys = 0;

    ReadFully(in, &version, sizeof(version), path);
    ReadFully(in, &reserved, sizeof(reserved), path);
    ReadFully(in, &nkeys, sizeof(nkeys), path);

    if (version != 1) {
      throw std::runtime_error("LoadKeyTraceFile: unsupported KTRC version=" +
                               std::to_string(version) + " path=" + path);
    }
    if (nkeys == 0) {
      throw std::runtime_error("LoadKeyTraceFile: KTRC nkeys=0 path=" + path);
    }

    const std::streamoff expect =
        static_cast<std::streamoff>(4 + sizeof(version) + sizeof(reserved) + sizeof(nkeys)) +
        static_cast<std::streamoff>(nkeys) * static_cast<std::streamoff>(sizeof(uint64_t));
    if (fsize < expect) {
      throw std::runtime_error("LoadKeyTraceFile: KTRC truncated (size mismatch) path=" + path);
    }

    key_trace_.resize(static_cast<size_t>(nkeys));
    ReadFully(in, key_trace_.data(), static_cast<size_t>(nkeys) * sizeof(uint64_t), path);
    return;
  }

  // ---- Case B: raw uint64 binary (no header) ----
  in.seekg(0, std::ios::beg);
  if (fsize > 0 && (fsize % static_cast<std::streamoff>(sizeof(uint64_t)) == 0)) {
    const size_t nkeys = static_cast<size_t>(fsize / static_cast<std::streamoff>(sizeof(uint64_t)));
    if (nkeys == 0) {
      throw std::runtime_error("LoadKeyTraceFile: raw-binary has 0 keys: " + path);
    }
    key_trace_.resize(nkeys);
    ReadFully(in, key_trace_.data(), nkeys * sizeof(uint64_t), path);
    return;
  }

  // ---- Case C: text fallback (one integer per line, allow # comments) ----
  in.close();
  std::ifstream txt(path);
  if (!txt.is_open()) {
    throw std::runtime_error("LoadKeyTraceFile: cannot reopen as text: " + path);
  }

  std::string line;
  while (std::getline(txt, line)) {
    // 去掉行尾 \r（Windows 檔案常見）
    if (!line.empty() && line.back() == '\r') line.pop_back();

    // 去掉 inline comment
    auto hash = line.find('#');
    if (hash != std::string::npos) line = line.substr(0, hash);

    line = Trim(line);
    if (line.empty()) continue;

    // 允許前後空白、但內容要是 uint64
    uint64_t v = 0;
    try {
      v = static_cast<uint64_t>(std::stoull(line));
    } catch (...) {
      throw std::runtime_error("LoadKeyTraceFile: invalid line: '" + line + "' in " + path);
    }
    key_trace_.push_back(v);
  }

  if (key_trace_.empty()) {
    throw std::runtime_error("LoadKeyTraceFile: text trace empty: " + path);
  }
}



class TraceGenerator : public Generator<uint64_t> {
 public:
  TraceGenerator(const std::string& path, bool loop)
      : loop_(loop) {
    std::ifstream in(path);
    if (!in.is_open()) {
      throw utils::Exception("Cannot open keytracefile: " + path);
    }

    uint64_t x;
    while (in >> x) {
      keys_.push_back(x);
    }
    if (keys_.empty()) {
      throw utils::Exception("keytracefile is empty: " + path);
    }
    last_.store(keys_[0], std::memory_order_relaxed);
  }

  uint64_t Next() override {
    uint64_t i = idx_.fetch_add(1, std::memory_order_relaxed);

    uint64_t v;
    if (loop_) {
      v = keys_[i % keys_.size()];
    } else {
      if (i >= keys_.size()) v = keys_.back();
      else v = keys_[i];
    }

    last_.store(v, std::memory_order_relaxed);
    return v;
  }

  uint64_t Last() override {
    return last_.load(std::memory_order_relaxed);
  }

 private:
  std::vector<uint64_t> keys_;
  bool loop_;
  std::atomic<uint64_t> idx_{0};
  std::atomic<uint64_t> last_{0};
};

} // namespace ycsbc

 


void CoreWorkload::Init(const utils::Properties &p) {
  table_name_ = p.GetProperty(TABLENAME_PROPERTY,TABLENAME_DEFAULT);
  
  field_count_ = std::stoi(p.GetProperty(FIELD_COUNT_PROPERTY,
                                         FIELD_COUNT_DEFAULT));
  field_len_generator_ = GetFieldLenGenerator(p);
  
  double read_proportion = std::stod(p.GetProperty(READ_PROPORTION_PROPERTY,
                                                   READ_PROPORTION_DEFAULT));
  double update_proportion = std::stod(p.GetProperty(UPDATE_PROPORTION_PROPERTY,
                                                     UPDATE_PROPORTION_DEFAULT));
  double insert_proportion = std::stod(p.GetProperty(INSERT_PROPORTION_PROPERTY,
                                                     INSERT_PROPORTION_DEFAULT));
  double scan_proportion = std::stod(p.GetProperty(SCAN_PROPORTION_PROPERTY,
                                                   SCAN_PROPORTION_DEFAULT));
  double readmodifywrite_proportion = std::stod(p.GetProperty(
      READMODIFYWRITE_PROPORTION_PROPERTY, READMODIFYWRITE_PROPORTION_DEFAULT));
  
  record_count_ = std::stoi(p.GetProperty(RECORD_COUNT_PROPERTY));
  std::string request_dist = p.GetProperty(REQUEST_DISTRIBUTION_PROPERTY,
                                           REQUEST_DISTRIBUTION_DEFAULT);
  zero_padding_ = std::stoi(p.GetProperty(ZERO_PADDING_PROPERTY, ZERO_PADDING_DEFAULT));
  int max_scan_len = std::stoi(p.GetProperty(MAX_SCAN_LENGTH_PROPERTY,
                                             MAX_SCAN_LENGTH_DEFAULT));
  std::string scan_len_dist = p.GetProperty(SCAN_LENGTH_DISTRIBUTION_PROPERTY,
                                            SCAN_LENGTH_DISTRIBUTION_DEFAULT);
  int insert_start = std::stoi(p.GetProperty(INSERT_START_PROPERTY,
                                             INSERT_START_DEFAULT));
  
  read_all_fields_ = utils::StrToBool(p.GetProperty(READ_ALL_FIELDS_PROPERTY,
                                                    READ_ALL_FIELDS_DEFAULT));
  write_all_fields_ = utils::StrToBool(p.GetProperty(WRITE_ALL_FIELDS_PROPERTY,
                                                     WRITE_ALL_FIELDS_DEFAULT));
  
  if (p.GetProperty(INSERT_ORDER_PROPERTY, INSERT_ORDER_DEFAULT) == "hashed") {
    ordered_inserts_ = false;
  } else {
    ordered_inserts_ = true;
  }
  
  const std::string trace = p.GetProperty(KEY_TRACE_FILE_PROPERTY, KEY_TRACE_FILE_DEFAULT);
  key_trace_loop_ = (p.GetProperty(KEY_TRACE_LOOP_PROPERTY, KEY_TRACE_LOOP_DEFAULT) == "true");

  if (!trace.empty() && trace != "none") {
    LoadKeyTraceFile(trace);
    use_key_trace_ = true;
  } else {
    use_key_trace_ = false;
  }


  key_generator_ = new CounterGenerator(insert_start);
  
  if (read_proportion > 0) {
    op_chooser_.AddValue(READ, read_proportion);
  }
  if (update_proportion > 0) {
    op_chooser_.AddValue(UPDATE, update_proportion);
  }
  if (insert_proportion > 0) {
    op_chooser_.AddValue(INSERT, insert_proportion);
  }
  if (scan_proportion > 0) {
    op_chooser_.AddValue(SCAN, scan_proportion);
  }
  if (readmodifywrite_proportion > 0) {
    op_chooser_.AddValue(READMODIFYWRITE, readmodifywrite_proportion);
  }
  
  insert_key_sequence_.Set(record_count_);
  
  if (request_dist == "uniform") {
    key_chooser_ = new UniformGenerator(0, record_count_ - 1);
    
  } else if (request_dist == "zipfian") {
    if (use_key_trace_) {
      key_chooser_ = new VectorTraceGenerator(&key_trace_, key_trace_loop_);
    } else {
      int op_count = std::stoi(p.GetProperty(OPERATION_COUNT_PROPERTY));
      int new_keys = (int)(op_count * insert_proportion * 2);
      key_chooser_ = new ScrambledZipfianGenerator(record_count_ + new_keys);
    }

  } else if (request_dist == "latest") {
    key_chooser_ = new SkewedLatestGenerator(insert_key_sequence_);
    
  } else {
    throw utils::Exception("Unknown request distribution: " + request_dist);
  }
  
  field_chooser_ = new UniformGenerator(0, field_count_ - 1);
  
  if (scan_len_dist == "uniform") {
    scan_len_chooser_ = new UniformGenerator(1, max_scan_len);
  } else if (scan_len_dist == "zipfian") {
    scan_len_chooser_ = new ZipfianGenerator(1, max_scan_len);
  } else {
    throw utils::Exception("Distribution not allowed for scan length: " +
        scan_len_dist);
  }
}

ycsbc::Generator<uint64_t> *CoreWorkload::GetFieldLenGenerator(
    const utils::Properties &p) {
  string field_len_dist = p.GetProperty(FIELD_LENGTH_DISTRIBUTION_PROPERTY,
                                        FIELD_LENGTH_DISTRIBUTION_DEFAULT);
  int field_len = std::stoi(p.GetProperty(FIELD_LENGTH_PROPERTY,
                                          FIELD_LENGTH_DEFAULT));
  if(field_len_dist == "constant") {
    return new ConstGenerator(field_len);
  } else if(field_len_dist == "uniform") {
    return new UniformGenerator(1, field_len);
  } else if(field_len_dist == "zipfian") {
    return new ZipfianGenerator(1, field_len);
  } else {
    throw utils::Exception("Unknown field length distribution: " +
        field_len_dist);
  }
}

void CoreWorkload::BuildValues(std::vector<ycsbc::DB::KVPair> &values) {
  for (int i = 0; i < field_count_; ++i) {
    ycsbc::DB::KVPair pair;
    pair.first.append("field").append(std::to_string(i));
    pair.second.append(field_len_generator_->Next(), utils::RandomPrintChar());
    values.push_back(pair);
  }
}

void CoreWorkload::BuildUpdate(std::vector<ycsbc::DB::KVPair> &update) {
  ycsbc::DB::KVPair pair;
  pair.first.append(NextFieldName());
  pair.second.append(field_len_generator_->Next(), utils::RandomPrintChar());
  update.push_back(pair);
}

