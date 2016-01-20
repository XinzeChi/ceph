// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

#include "acconfig.h"

#include "os/LruWBThrottle.h"
#include "common/perf_counters.h"
#include "common/io_priority.h"

#define dout_subsys ceph_subsys_filestore
#undef dout_prefix
#define dout_prefix *_dout << "WBThrottle "

LruWBThrottle::LruWBThrottle(CephContext *cct, string n) :
  WBThrottle(cct, n)
{
}

LruWBThrottle::~LruWBThrottle()
{
}

bool LruWBThrottle::get_next_should_flush(
  boost::tuple<ghobject_t, FDRef, PendingWB> *next)
{
  assert(lock.is_locked());
  assert(next);
  while (!stopping &&
         cur_ios < io_limits.first &&
         pending_wbs.size() < fd_limits.first &&
         cur_size < size_limits.first)
         cond.Wait(lock);
  if (stopping)
    return false;
  assert(!pending_wbs.empty());
  ghobject_t obj(pop_object());
  
  ceph::unordered_map<ghobject_t, pair<PendingWB, FDRef> >::iterator i =
    pending_wbs.find(obj);
  *next = boost::make_tuple(obj, i->second.second, i->second.first);
  pending_wbs.erase(i);
  return true;
}

void LruWBThrottle::queue_wb(
  FDRef fd, const ghobject_t &hoid, uint64_t offset, uint64_t len,
  bool nocache)
{
  Mutex::Locker l(lock);
  ceph::unordered_map<ghobject_t, pair<PendingWB, FDRef> >::iterator wbiter =
    pending_wbs.find(hoid);
  if (wbiter == pending_wbs.end()) {
    wbiter = pending_wbs.insert(
      make_pair(hoid,
	make_pair(
	  PendingWB(),
	  fd))).first;
    logger->inc(l_wbthrottle_inodes_dirtied);
  } else {
    remove_object(hoid);
  }

  cur_ios++;
  logger->inc(l_wbthrottle_ios_dirtied);
  cur_size += len;
  logger->inc(l_wbthrottle_bytes_dirtied, len);

  wbiter->second.first.add(nocache, len, 1);
  insert_object(hoid);
  cond.Signal();
}

void LruWBThrottle::clear()
{
  Mutex::Locker l(lock);
  for (ceph::unordered_map<ghobject_t, pair<PendingWB, FDRef> >::iterator i =
	 pending_wbs.begin();
       i != pending_wbs.end();
       ++i) {
#ifdef HAVE_POSIX_FADVISE
    if (g_conf->filestore_fadvise && i->second.first.nocache) {
      int fa_r = posix_fadvise(**i->second.second, 0, 0, POSIX_FADV_DONTNEED);
      assert(fa_r == 0);
    }
#endif

    cur_ios -= i->second.first.ios;
    logger->dec(l_wbthrottle_ios_dirtied, i->second.first.ios);
    cur_size -= i->second.first.size;
    logger->dec(l_wbthrottle_bytes_dirtied, i->second.first.size);
    logger->dec(l_wbthrottle_inodes_dirtied);
  }
  pending_wbs.clear();
  lru.clear();
  rev_lru.clear();
  cond.Signal();
}

void LruWBThrottle::clear_object(const ghobject_t &hoid)
{
  Mutex::Locker l(lock);
  while (clearing && *clearing == hoid)
    cond.Wait(lock);
  ceph::unordered_map<ghobject_t, pair<PendingWB, FDRef> >::iterator i =
    pending_wbs.find(hoid);
  if (i == pending_wbs.end())
    return;

  cur_ios -= i->second.first.ios;
  logger->dec(l_wbthrottle_ios_dirtied, i->second.first.ios);
  cur_size -= i->second.first.size;
  logger->dec(l_wbthrottle_bytes_dirtied, i->second.first.size);
  logger->dec(l_wbthrottle_inodes_dirtied);

  pending_wbs.erase(i);
  remove_object(hoid);
}
