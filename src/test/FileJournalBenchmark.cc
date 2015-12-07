// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*- 
// vim: ts=8 sw=2 smarttab
/*
 * Ceph - scalable distributed file system
 *
 * Copyright (C) 2015 UnitedStack <haomai@unitedstack.com>
 *
 * Author: Haomai Wang <haomaiwang@gmail.com>
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1, as published by the Free Software
 * Foundation.  See file COPYING.
 *
 */

#include <stdlib.h>
#include <stdint.h>
#include <string>
#include <iostream>

using namespace std;

#include "common/ceph_argparse.h"
#include "common/common_init.h"
#include "global/global_init.h"
#include "common/Finisher.h"
#include "include/Context.h"
#include "common/Mutex.h" 
#include "common/debug.h"
#include "common/Cycles.h"
#include "global/global_init.h"
#include "os/FileJournal.h"
#include "os/JournalingObjectStore.h"

class PerfCase {
  FileJournal journal;
  vector<int> corr_sizes;
  map<int, bufferlist> data;
  Cond sync_cond;
  uint64_t count;
  uint64_t cycles;
  string name;
  uint64_t seq;

  class CompletionFinisher : public Context {
    PerfCase *c;
    uint64_t start_c;
    uint64_t size;
    uint64_t seq;

   public:
    CompletionFinisher(PerfCase *c, uint64_t s, uint64_t seq): c(c),
      start_c(Cycles::rdtsc()), size(s), seq(seq) { }
    ~CompletionFinisher() {}

    void finish(int r) {
      Mutex::Locker l(c->lock);
      c->count++;
      c->cycles += Cycles::rdtsc() - start_c;
      if (seq > c->seq) {
        c->seq = seq;
        if (seq % 1000 == 0)
          c->journal.committed_thru(seq);
      }
    }
  };
 public:
  Mutex lock;
  PerfCase(const char *path, vector<pair<int, int> > &size_proportion, Finisher *f, bool dio, bool aio):
      journal(uuid_d(), f, &sync_cond, path, dio, aio, aio),
      count(0), cycles(0), seq(0), lock("PerfCase::lock") {
    int i = 0;
    corr_sizes.resize(100);

    name += "dio=";
    if (aio)
      name += "true";
    else
      name += "false";
    name += ",aio=";
    if (dio)
      name += "true";
    else
      name += "false";
    for (vector<pair<int, int> >::iterator it = size_proportion.begin();
         it != size_proportion.end(); ++it) {
      bufferlist bl;
      bufferptr bp = buffer::create_page_aligned(it->second);
      bp.zero();
      for (uint64_t j = 0; j < it->second-sizeof(it->second);
           j += sizeof(it->second)) {
        memcpy(bp.c_str()+j, &it->second, sizeof(it->second));
      }
      bl.append(bp);
      data[it->second] = bl;

      for (int j = 0; j < it->first; ++j, ++i)
        corr_sizes[i] = it->second;

      char buf[100];
      snprintf(buf, sizeof(buf), " %7d(%d%%)", it->second, it->first);
      name += string(buf);
    }
    assert(i == 100);
  }
  ~PerfCase() {
    journal.close();
    cerr << name << " total=" << Cycles::to_microseconds(cycles)
         << "us avg=" << Cycles::to_microseconds(cycles)/count << "us" << std::endl;
  }

  void run(int nums, int iodepth) {
    journal.create();
    journal.make_writeable();
    srand(time(NULL));
    // precalculate random number
    vector<int> random_nums;
    for (int i = 0; i < nums; ++i)
      random_nums.push_back(rand() % 100);

    uint64_t seq = 0;
    list<ObjectStore::Transaction*> tls;
    for (vector<int>::iterator it = random_nums.begin();
         it != random_nums.end(); ++it) {
      bufferlist bl = data[corr_sizes[*it]];
      int orig_len = journal.prepare_entry(tls, &bl);
      journal.submit_entry(seq, bl, orig_len, new CompletionFinisher(this, corr_sizes[*it], seq));
      seq++;
      while (count + iodepth <= seq && seq != 1) {
        usleep(1);
      }
    }

    while (count != (uint64_t)nums)
      sleep(1);
  }
};

void usage(const string &name) {
  cerr << "Usage: " << name << " [path] [times] [iodepth]" << std::endl;
}

int main(int argc, char **argv)
{
  vector<const char*> args;
  argv_to_vec(argc, (const char **)argv, args);

  global_init(NULL, args, CEPH_ENTITY_TYPE_CLIENT, CODE_ENVIRONMENT_UTILITY, 0);
  common_init_finish(g_ceph_context);
  g_ceph_context->_conf->apply_changes(NULL);

  cerr << "args: " << args << std::endl;
  if (args.size() < 3) {
    usage(argv[0]);
    return 1;
  }

  const char *path = args[0];
  uint64_t times = atoi(args[1]);
  uint64_t iodepth = atoi(args[2]);

  Finisher *finisher = new Finisher(g_ceph_context);

  finisher->start();
  bool aio = true;
  bool dio = true;
  uint64_t Kib = 1024;
  uint64_t Mib = 1024 * 1024;
  cerr << "iodepth=" << iodepth << " count=" << times << " started:" << std::endl;
  while (1) {
    vector<pair<int, int> > sizes;
    sizes.push_back(make_pair(100, 4 * Kib));
    sizes.push_back(make_pair(100, 16 * Kib));
    for (vector<pair<int, int> >::iterator it = sizes.begin();
         it != sizes.end();) {
      uint64_t used = 0;
      vector<pair<int, int> > proportion;
      while (used != 100) {
        proportion.push_back(*it);
        used += it->first;
        ++it;
      }
      PerfCase c(path, proportion, finisher, dio, aio);
      c.run(times, iodepth);
    }
  }

  finisher->stop();
  unlink(args[0]);
  return 0;
}
