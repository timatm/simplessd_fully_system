//
//  ycsbc.cc
//  YCSB-C
//
//  Created by Jinglei Ren on 12/19/14.
//  Copyright (c) 2014 Jinglei Ren <jinglei@ren.systems>.
//

#include <cstring>
#include <string>
#include <iostream>
#include <vector>
#include <future>
#include "core/utils.h"
#include "core/timer.h"
#include "core/client.h"
#include "core/core_workload.h"
#include "db/db_factory.h"
#include <iostream>
#include <iomanip>
// #include <gem5/m5ops.h> 

using namespace std;

void UsageMessage(const char *command);
bool StrStartWith(const char *str, const char *pre);
string ParseCommandLine(int argc, const char *argv[], utils::Properties &props);

int DelegateClient(ycsbc::DB *db, ycsbc::CoreWorkload *wl, const int num_ops,
    bool is_loading) {
  db->Init();
  ycsbc::Client client(*db, *wl);
  int oks = 0;
  for (int i = 0; i < num_ops; ++i) {
    if (is_loading) {
      oks += client.DoInsert();
    } else {
      oks += client.DoTransaction();
    }
  }
  db->Close();
  return oks;
}

static inline struct timespec ts_sub(struct timespec a, struct timespec b){
  struct timespec d;
  if ((a.tv_nsec -= b.tv_nsec) < 0) { a.tv_nsec += 1000000000; a.tv_sec -= 1; }
  d.tv_sec = a.tv_sec - b.tv_sec; d.tv_nsec = a.tv_nsec;
  return d;
}



int main(const int argc, const char *argv[]) {
  utils::Properties props;
  string file_name = ParseCommandLine(argc, argv, props);

  ycsbc::DB *db = ycsbc::DBFactory::CreateDB(props);
  if (!db) {
    cout << "Unknown database name " << props["dbname"] << endl;
    exit(0);
  }

  ycsbc::CoreWorkload wl;
  wl.Init(props);

  const int num_threads = stoi(props.GetProperty("threadcount", "1"));

  // Loads data
  vector<future<int>> actual_ops;
  int total_ops = stoi(props[ycsbc::CoreWorkload::RECORD_COUNT_PROPERTY]);
  for (int i = 0; i < num_threads; ++i) {
    actual_ops.emplace_back(async(launch::async,
        DelegateClient, db, &wl, total_ops / num_threads, true));
  }
  assert((int)actual_ops.size() == num_threads);

  int sum = 0;
  for (auto &n : actual_ops) {
    assert(n.valid());
    sum += n.get();
  }
  cerr << "# Loading records:\t" << sum << endl;

  // Peforms transactions
  actual_ops.clear();
  total_ops = stoi(props[ycsbc::CoreWorkload::OPERATION_COUNT_PROPERTY]);

  // utils::sanity_timer_200ms();
  // utils::PortableTimer::Sanity200ms();   // 打印一下；若仍为 0，说明只能走 fallback
  utils::PortableTimer timer;
  struct timespec t0{}, t1{};
  if (clock_gettime(CLOCK_MONOTONIC, &t0) != 0) { perror("t0 clock_gettime"); return 1; }
  // m5_reset_stats(0, 0);
  // utils::MonotonicTimer timer;
  timer.Start();
  for (int i = 0; i < num_threads; ++i) {
    actual_ops.emplace_back(async(launch::async,
        DelegateClient, db, &wl, total_ops / num_threads, false));
  }



  // actual_ops.clear();
  // total_ops = stoi(props[ycsbc::CoreWorkload::OPERATION_COUNT_PROPERTY]);
  // utils::Timer timer;
  // timer.Start();
  // for (int i = 0; i < num_threads; ++i) {
  //   actual_ops.emplace_back(async(launch::async,
  //       DelegateClient, db, &wl, total_ops / num_threads, false));
  // }
  assert((int)actual_ops.size() == num_threads);

  sum = 0;
  for (auto &n : actual_ops) {
    assert(n.valid());
    sum += n.get();
  }
  double duration = timer.End();
  if (clock_gettime(CLOCK_MONOTONIC, &t1) != 0) { perror("t1 clock_gettime"); return 1; }


  auto ts_sub = [](timespec a, timespec b){
    if ((a.tv_nsec -= b.tv_nsec) < 0) { a.tv_nsec += 1000000000; a.tv_sec -= 1; }
    timespec d; d.tv_sec = a.tv_sec - b.tv_sec; d.tv_nsec = a.tv_nsec; return d;
  };
  timespec d = ts_sub(t1, t0);
  unsigned long long ns = (unsigned long long)d.tv_sec * 1000000000ull
                        + (unsigned long long)d.tv_nsec;

  // 强制浮点：不要用整数链式再乘/除
  double secs = (double)ns / 1e9;
  double ms   = (double)ns / 1e6;


  std::cout << "time nano second=" << ns << std::endl;

  std::cout << std::fixed << std::setprecision(3)
            << "elapsed_ms=" << (ns / 1000000.0) << " ";
  std::cout << std::fixed << std::setprecision(6)
            << "duration_s=" << (ns / 1000000000.0) << "\n";


  // 用同一个 secs 算吞吐，避免“两个计时器两个结果”
  double ktps = (secs > 0.0) ? ( (double)sum / secs / 1000.0 ) : 0.0;

  // m5_dump_stats(0, 0);
  // double ktps = (duration > 0.0) ? (static_cast<double>(sum) / duration / 1000.0) : 0.0;

  cerr.setf(std::ios::fixed);
  cerr.precision(3);
  cerr << "# Transaction throughput (KTPS)\n";
  cerr << props["dbname"] << '\t' << file_name << '\t' << num_threads << '\t' << ktps << '\n';

  cerr << "total_ops: " << sum << '\n';  // 打印“实际完成”的操作数
  cerr.precision(6);
  cerr << "duration: " << duration << " s\n";

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
  cout << "  -P propertyfile: load properties from the given file. Multiple files can" << endl;
  cout << "                   be specified, and will be processed in the order specified" << endl;
}

inline bool StrStartWith(const char *str, const char *pre) {
  return strncmp(str, pre, strlen(pre)) == 0;
}

