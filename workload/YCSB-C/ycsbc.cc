// //
// //  ycsbc.cc
// //  YCSB-C
// //
// //  Created by Jinglei Ren on 12/19/14.
// //  Copyright (c) 2014 Jinglei Ren <jinglei@ren.systems>.
// //
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <future>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include "core/utils.h"
#include "core/timer.h"
#include "core/client.h"
#include "core/core_workload.h"
#include "db/db_factory.h"
#include "print.hh"
// #include <gem5/m5ops.h>

using namespace std;

struct LatencyStats {
  uint64_t count = 0;
  uint64_t ok_count = 0;
  uint64_t total_ns = 0;
  uint64_t max_ns = 0;
  std::vector<uint64_t> samples_ns;
};

struct ThreadStats {
  int oks = 0;
  LatencyStats put;
  LatencyStats get;
  LatencyStats update;
  LatencyStats other;
};

void UsageMessage(const char *command);
bool StrStartWith(const char *str, const char *pre);
string ParseCommandLine(int argc, const char *argv[], utils::Properties &props);

static inline void RecordLatency(LatencyStats &stats, bool ok, uint64_t latency_ns) {
  ++stats.count;
  if (ok) {
    ++stats.ok_count;
  }
  stats.total_ns += latency_ns;
  if (latency_ns > stats.max_ns) {
    stats.max_ns = latency_ns;
  }
  stats.samples_ns.push_back(latency_ns);
}

static inline void MergeLatency(LatencyStats &dst, const LatencyStats &src) {
  dst.count += src.count;
  dst.ok_count += src.ok_count;
  dst.total_ns += src.total_ns;
  if (src.max_ns > dst.max_ns) {
    dst.max_ns = src.max_ns;
  }
  dst.samples_ns.insert(dst.samples_ns.end(), src.samples_ns.begin(), src.samples_ns.end());
}

static inline void MergeThreadStats(ThreadStats &dst, const ThreadStats &src) {
  dst.oks += src.oks;
  MergeLatency(dst.put, src.put);
  MergeLatency(dst.get, src.get);
  MergeLatency(dst.update, src.update);
  MergeLatency(dst.other, src.other);
}

static std::vector<int> SplitOps(int total_ops, int num_threads) {
  std::vector<int> per_thread(num_threads, total_ops / num_threads);
  int remainder = total_ops % num_threads;
  for (int i = 0; i < remainder; ++i) {
    ++per_thread[i];
  }
  return per_thread;
}

static uint64_t PercentileNs(std::vector<uint64_t> &sorted_ns, double pct) {
  if (sorted_ns.empty()) {
    return 0;
  }
  if (pct <= 0.0) {
    return sorted_ns.front();
  }
  if (pct >= 100.0) {
    return sorted_ns.back();
  }

  double rank = std::ceil((pct / 100.0) * static_cast<double>(sorted_ns.size()));
  size_t idx = static_cast<size_t>(rank);
  if (idx == 0) {
    idx = 1;
  }
  idx -= 1;
  if (idx >= sorted_ns.size()) {
    idx = sorted_ns.size() - 1;
  }
  return sorted_ns[idx];
}

static void PrintOneLatencySummary(const std::string &name, LatencyStats &stats) {
  std::sort(stats.samples_ns.begin(), stats.samples_ns.end());

  const double total_ms = static_cast<double>(stats.total_ns) / 1e6;
  const double avg_us = stats.count ? (static_cast<double>(stats.total_ns) / stats.count / 1e3) : 0.0;
  const double p50_us = static_cast<double>(PercentileNs(stats.samples_ns, 50.0)) / 1e3;
  const double p90_us = static_cast<double>(PercentileNs(stats.samples_ns, 90.0)) / 1e3;
  const double p95_us = static_cast<double>(PercentileNs(stats.samples_ns, 95.0)) / 1e3;
  const double p99_us = static_cast<double>(PercentileNs(stats.samples_ns, 99.0)) / 1e3;
  const double p999_us = static_cast<double>(PercentileNs(stats.samples_ns, 99.9)) / 1e3;
  const double p9999_us = static_cast<double>(PercentileNs(stats.samples_ns, 99.99)) / 1e3;
  const double max_us = static_cast<double>(stats.max_ns) / 1e3;

  cout << fixed << setprecision(3)
       << name
       << ": count=" << stats.count
       << " ok=" << stats.ok_count
       << " total_ms=" << total_ms
       << " avg_us=" << avg_us
       << " p50_us=" << p50_us
       << " p90_us=" << p90_us
       << " p95_us=" << p95_us
       << " p99_us=" << p99_us
       << " p99.9_us=" << p999_us
       << " p99.99_us=" << p9999_us
       << " max_us=" << max_us
       << '\n';
}

static ThreadStats DelegateClient(ycsbc::DB *db,
                                  ycsbc::CoreWorkload *wl,
                                  int num_ops,
                                  bool is_loading) {
  ycsbc::Client client(*db, *wl);
  ThreadStats stats;

  for (int i = 0; i < num_ops; ++i) {
    const auto start = std::chrono::steady_clock::now();

    if (is_loading) {
      const bool ok = client.DoInsert();
      const auto end = std::chrono::steady_clock::now();
      const uint64_t latency_ns =
          static_cast<uint64_t>(
              std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count());

      RecordLatency(stats.put, ok, latency_ns);
      if (ok) {
        ++stats.oks;
      }
    } else {
      ycsbc::Operation op = ycsbc::READ;
      const bool ok = client.DoTransaction(&op);
      const auto end = std::chrono::steady_clock::now();
      const uint64_t latency_ns =
          static_cast<uint64_t>(
              std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count());

      switch (op) {
        case ycsbc::INSERT:
          RecordLatency(stats.put, ok, latency_ns);
          break;
        case ycsbc::READ:
          RecordLatency(stats.get, ok, latency_ns);
          break;
        case ycsbc::UPDATE:
          RecordLatency(stats.update, ok, latency_ns);
          break;
        case ycsbc::SCAN:
        case ycsbc::READMODIFYWRITE:
        default:
          RecordLatency(stats.other, ok, latency_ns);
          break;
      }

      if (ok) {
        ++stats.oks;
      }
    }
  }

  return stats;
}

int main(const int argc, const char *argv[]) {
  utils::Properties props;
  string file_name = ParseCommandLine(argc, argv, props);

  ycsbc::DB *db = ycsbc::DBFactory::CreateDB(props);
  if (!db) {
    cout << "Unknown database name " << props["dbname"] << endl;
    exit(0);
  }

  db->Init();

  ycsbc::CoreWorkload wl;
  pr_info("before wl.Init");
  wl.Init(props);
  pr_info("after wl.Init");

  const int num_threads = stoi(props.GetProperty("threadcount", "1"));
  const bool run_only  = (props.GetProperty("runonly",  "false") == "true");
  const bool load_only = (props.GetProperty("loadonly", "false") == "true");
  const bool do_load = !run_only;
  const bool do_run  = !load_only;

  ThreadStats load_stats_all;
  ThreadStats txn_stats_all;
  double load_wall_ms = 0.0;
  double run_wall_ms = 0.0;

  // ----------------------------
  // Load phase
  // ----------------------------
  if (do_load) {
    const int total_ops = stoi(props[ycsbc::CoreWorkload::RECORD_COUNT_PROPERTY]);
    const std::vector<int> per_thread = SplitOps(total_ops, num_threads);
    std::vector<std::future<ThreadStats> > futures;
    futures.reserve(num_threads);

    const auto load_start = std::chrono::steady_clock::now();

    for (int i = 0; i < num_threads; ++i) {
      futures.emplace_back(std::async(std::launch::async,
                                      DelegateClient, db, &wl, per_thread[i], true));
    }

    for (size_t i = 0; i < futures.size(); ++i) {
      assert(futures[i].valid());
      MergeThreadStats(load_stats_all, futures[i].get());
    }

    const auto load_end = std::chrono::steady_clock::now();
    load_wall_ms =
        std::chrono::duration<double, std::milli>(load_end - load_start).count();

    cerr << "# Loading records:\t" << load_stats_all.oks << endl;
    cout << fixed << setprecision(3)
         << "load_elapsed_ms=" << load_wall_ms << '\n';
    PrintOneLatencySummary("LOAD_PUT", load_stats_all.put);
  } else {
    cerr << "# Skip loading phase, reuse existing DB.\n";
  }

  // ----------------------------
  // Transaction phase
  // ----------------------------
  if (do_run) {
    cout << "# Run start"  << '\n';
    const int total_ops = stoi(props[ycsbc::CoreWorkload::OPERATION_COUNT_PROPERTY]);
    const std::vector<int> per_thread = SplitOps(total_ops, num_threads);
    std::vector<std::future<ThreadStats> > futures;
    futures.reserve(num_threads);

    const auto run_start = std::chrono::steady_clock::now();

    for (int i = 0; i < num_threads; ++i) {
      futures.emplace_back(std::async(std::launch::async,
                                      DelegateClient, db, &wl, per_thread[i], false));
    }

    for (size_t i = 0; i < futures.size(); ++i) {
      assert(futures[i].valid());
      MergeThreadStats(txn_stats_all, futures[i].get());
    }

    const auto run_end = std::chrono::steady_clock::now();

    const auto run_ns_chrono =
        std::chrono::duration_cast<std::chrono::nanoseconds>(run_end - run_start).count();
    const unsigned long long ns = static_cast<unsigned long long>(run_ns_chrono);
    const double secs = static_cast<double>(ns) / 1e9;
    const double duration = secs;
    run_wall_ms = static_cast<double>(ns) / 1e6;

    const double ktps =
        (secs > 0.0) ? (static_cast<double>(txn_stats_all.oks) / secs / 1000.0) : 0.0;

    cout << "time nano second=" << ns << endl;
    cout << fixed << setprecision(3)
         << "elapsed_ms=" << (static_cast<double>(ns) / 1000000.0) << ' ';
    cout << fixed << setprecision(6)
         << "duration_s=" << (static_cast<double>(ns) / 1000000000.0) << "\n";

    cerr.setf(std::ios::fixed);
    cerr.precision(3);
    cerr << "# Transaction throughput (KTPS)\n";
    cerr << props["dbname"] << '\t' << file_name << '\t' << num_threads << '\t' << ktps << '\n';

    cerr << "total_ops: " << txn_stats_all.oks << '\n';
    cerr.precision(6);
    cerr << "duration: " << duration << " s\n";

    pr_stat("time_ns=%lld db=%s file=%s threads=%d total_ops=%lld ktps=%.3f",
            static_cast<long long>(ns),
            props["dbname"].c_str(),
            file_name.c_str(),
            num_threads,
            static_cast<long long>(txn_stats_all.oks),
            ktps);

    cout << "# Transaction latency summary\n";
    PrintOneLatencySummary("PUT", txn_stats_all.put);
    PrintOneLatencySummary("GET", txn_stats_all.get);
    PrintOneLatencySummary("UPDATE", txn_stats_all.update);
    PrintOneLatencySummary("OTHER", txn_stats_all.other);

    const double load_put_total_ms =
        static_cast<double>(load_stats_all.put.total_ns) / 1e6;
    const double txn_put_total_ms =
        static_cast<double>(txn_stats_all.put.total_ns) / 1e6;
    const double put_total_ms_all = load_put_total_ms + txn_put_total_ms;

    const double get_total_ms =
        static_cast<double>(txn_stats_all.get.total_ns) / 1e6;
    const double update_total_ms =
        static_cast<double>(txn_stats_all.update.total_ns) / 1e6;
    const double other_total_ms =
        static_cast<double>(txn_stats_all.other.total_ns) / 1e6;

    const double sum_latency_ms_txn_only =
        txn_put_total_ms + get_total_ms + update_total_ms;
    const double sum_latency_ms_all =
        put_total_ms_all + get_total_ms + update_total_ms;

    cout << fixed << setprecision(3)
         << "load_put_total_ms=" << load_put_total_ms << '\n'
         << "txn_put_total_ms=" << txn_put_total_ms << '\n'
         << "put_total_ms_all=" << put_total_ms_all << '\n'
         << "get_total_ms=" << get_total_ms << '\n'
         << "update_total_ms=" << update_total_ms << '\n'
         << "other_total_ms=" << other_total_ms << '\n'
         << "sum_latency_ms_txn_only(put+get+update)=" << sum_latency_ms_txn_only << '\n'
         << "sum_latency_ms_all(put+get+update)=" << sum_latency_ms_all << '\n'
         << "run_wall_clock_ms=" << run_wall_ms << '\n'
         << "program_wall_clock_ms=" << (load_wall_ms + run_wall_ms) << '\n';
  } else {
    cerr << "# Skip transaction phase (loadonly).\n";
  }

  db->Close();
  delete db;
  return 0;
}

string ParseCommandLine(int argc, const char *argv[], utils::Properties &props) {
  int argindex = 1;
  string filename;

  while (argindex < argc && StrStartWith(argv[argindex], "-")) {
    if (strcmp(argv[argindex], "-threads") == 0) {
      argindex++;
      if (argindex >= argc) {
        UsageMessage(argv[0]);
        exit(0);
      }
      props.SetProperty("threadcount", argv[argindex]);
      argindex++;
    } else if (strcmp(argv[argindex], "-db") == 0) {
      argindex++;
      if (argindex >= argc) {
        UsageMessage(argv[0]);
        exit(0);
      }
      props.SetProperty("dbname", argv[argindex]);
      argindex++;
    } else if (strcmp(argv[argindex], "-host") == 0) {
      argindex++;
      if (argindex >= argc) {
        UsageMessage(argv[0]);
        exit(0);
      }
      props.SetProperty("host", argv[argindex]);
      argindex++;
    } else if (strcmp(argv[argindex], "-port") == 0) {
      argindex++;
      if (argindex >= argc) {
        UsageMessage(argv[0]);
        exit(0);
      }
      props.SetProperty("port", argv[argindex]);
      argindex++;
    } else if (strcmp(argv[argindex], "-slaves") == 0) {
      argindex++;
      if (argindex >= argc) {
        UsageMessage(argv[0]);
        exit(0);
      }
      props.SetProperty("slaves", argv[argindex]);
      argindex++;
    } else if (strcmp(argv[argindex], "-runonly") == 0) {
      props.SetProperty("runonly", "true");
      argindex++;
    } else if (strcmp(argv[argindex], "-loadonly") == 0) {
      props.SetProperty("loadonly", "true");
      argindex++;
    } else if (strcmp(argv[argindex], "-P") == 0) {
      argindex++;
      if (argindex >= argc) {
        UsageMessage(argv[0]);
        exit(0);
      }
      filename.assign(argv[argindex]);
      ifstream input(argv[argindex]);
      try {
        props.Load(input);
      } catch (const string &message) {
        cout << message << endl;
        exit(0);
      }
      input.close();
      argindex++;
    } else {
      cout << "Unknown option '" << argv[argindex] << "'" << endl;
      exit(0);
    }
  }

  if (argindex == 1 || argindex != argc) {
    UsageMessage(argv[0]);
    exit(0);
  }

  return filename;
}

void UsageMessage(const char *command) {
  cout << "Usage: " << command << " [options]" << endl;
  cout << "Options:" << endl;
  cout << "  -threads n: execute using n threads (default: 1)" << endl;
  cout << "  -db dbname: specify the name of the DB to use (default: basic)" << endl;
  cout << "  -runonly: only run transactions, skip loading phase" << endl;
  cout << "  -loadonly: only load records, skip transaction phase" << endl;
  cout << "  -P propertyfile: load properties from the given file. Multiple files can" << endl;
  cout << "                   be specified, and will be processed in the order specified" << endl;
}

inline bool StrStartWith(const char *str, const char *pre) {
  return strncmp(str, pre, strlen(pre)) == 0;
}