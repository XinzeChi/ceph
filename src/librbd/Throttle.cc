// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*- 
// vim: ts=8 sw=2 smarttab
/*
 * Ceph - scalable distributed file system
 *
 * Copyright (C) 2015 XSky <haomai@xsky.com>
 *
 * Author: Haomai Wang <haomai@xsky.com>
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1, as published by the Free Software
 * Foundation.  See file COPYING.
 *
 */

#include "common/Clock.h"
#include "Throttle.h"

#define dout_subsys ceph_subsys_rbd
#undef dout_prefix
#define dout_prefix *_dout << "librbd::BlockThrottle::"

BlockThrottle::BlockThrottle(CephContext *c, uint64_t op_size)
  : cct(c), op_size(op_size), lock("librbd::Throttle::BlockThrottle::lock"),
    timer(c, lock, true), enable(false) {
    timer_cb[0] = timer_cb[1] = NULL;
    timer_wait[0] = timer_wait[1] = false;
    timer.init();
  }

BlockThrottle::~BlockThrottle()
{
  {
    Mutex::Locker l(lock);
    timer.shutdown();
  }
  delete timer_cb[0];
  delete timer_cb[1];
}

/* Add timers to event loop */
void BlockThrottle::attach_context(Context *reader, Context *writer)
{
  delete timer_cb[0];
  delete timer_cb[1];
  timer_cb[0] = reader;
  timer_cb[1] = writer;
}

/* Does any throttling must be done
 *
 * @ret: true if throttling must be done else false
 */
static bool throttle_enabling(const LeakyBucket (&buckets)[BUCKETS_COUNT])
{
  int i;

  for (i = 0; i < BUCKETS_COUNT; i++) {
    if (buckets[i].avg > 0) {
      return true;
    }
  }

  return false;
}

/* return true if any two throttling parameters conflicts
 *
 * @ret: true if any conflict detected else false
 */
static bool throttle_conflicting(const LeakyBucket (&buckets)[BUCKETS_COUNT])
{
    bool bps_flag, ops_flag;
    bool bps_max_flag, ops_max_flag;

    bps_flag = buckets[THROTTLE_BPS_TOTAL].avg &&
               (buckets[THROTTLE_BPS_READ].avg ||
                buckets[THROTTLE_BPS_WRITE].avg);

    ops_flag = buckets[THROTTLE_OPS_TOTAL].avg &&
               (buckets[THROTTLE_OPS_READ].avg ||
                buckets[THROTTLE_OPS_WRITE].avg);

    bps_max_flag = buckets[THROTTLE_BPS_TOTAL].max &&
                  (buckets[THROTTLE_BPS_READ].max  ||
                   buckets[THROTTLE_BPS_WRITE].max);

    ops_max_flag = buckets[THROTTLE_OPS_TOTAL].max &&
                   (buckets[THROTTLE_OPS_READ].max ||
                   buckets[THROTTLE_OPS_WRITE].max);

    return bps_flag || ops_flag || bps_max_flag || ops_max_flag;
}

/* Used to configure the throttle
 *
 * @type: the throttle type we are working on
 * @avg: the config to set
 * @max: the config to set
 * @ret: true if any conflict detected else false
 */
bool BlockThrottle::config(BucketType type, double avg, double max)
{
  if (avg < 0 || max < 0)
    return false;
  Mutex::Locker l(lock);
  LeakyBucket local[BUCKETS_COUNT];
  memcpy(&local, buckets, sizeof(local));
  if (avg)
    local[type].avg = avg;
  if (max)
    local[type].max = max;
  if (throttle_conflicting(local))
    return false;

  buckets[type].avg = local[type].avg;
  // Ensure max value isn't zero if avg not zero
  buckets[type].max = MAX(local[type].avg, local[type].max);
  enable = throttle_enabling(buckets);
  return true;
}

/* Schedule the read or write timer if needed
 *
 * NOTE: this function is not unit tested due to it's usage of timer_mod
 *
 * @is_write: the type of operation (read/write)
 * @ret:      true if the timer has been scheduled else false
 */
bool BlockThrottle::schedule_timer(bool is_write, bool release_timer_wait)
{
  if (release_timer_wait)
    timer_wait[is_write] = false;
  else
    lock.Lock();

  /* leak proportionally to the time elapsed */
  throttle_do_leak();

  /* compute the wait time if any */
  double wait = throttle_compute_wait_for(is_write);

  /* if the code must wait compute when the next timer should fire */
  if (!wait) {
    if (!release_timer_wait)
      lock.Unlock();
    return false;
  }

  /* request throttled and timer not pending -> arm timer */
  if (!timer_wait[is_write]) {
    assert(timer_cb[is_write]);
    timer.add_event_after(wait, timer_cb[is_write]);
    timer_wait[is_write] = true;
  }
  if (!release_timer_wait)
    lock.Unlock();
  return true;
}

/* do the accounting for this operation
 *
 * @is_write: the type of operation (read/write)
 * @size:     the size of the operation
 */
void BlockThrottle::account(bool is_write, uint64_t size, bool lock_hold)
{
  if (!lock_hold)
    lock.Lock();
  double units = 1.0;

  /* if op_size is defined and smaller than size we compute unit count */
  if (op_size && size > op_size)
    units = (double) size / op_size;

  buckets[THROTTLE_BPS_TOTAL].level += size;
  buckets[THROTTLE_OPS_TOTAL].level += units;

  if (is_write) {
    buckets[THROTTLE_BPS_WRITE].level += size;
    buckets[THROTTLE_OPS_WRITE].level += units;
  } else {
    buckets[THROTTLE_BPS_READ].level += size;
    buckets[THROTTLE_OPS_READ].level += units;
  }
  if (!lock_hold)
    lock.Unlock();
}

/* This function make a bucket leak
 *
 * @bkt:   the bucket to make leak
 * @delta_ns: the time delta
 */
void BlockThrottle::throttle_leak_bucket(LeakyBucket *bkt, uint64_t delta_ns)
{
  /* compute how much to leak */
  double leak = (bkt->avg * (double) delta_ns) / NANOSECONDS_PER_SECOND;
  /* make the bucket leak */
  bkt->level = MAX(bkt->level - leak, 0);
}

/* Calculate the time delta since last leak and make proportionals leaks
 *
 * @now:      the current timestamp in ns
 */
void BlockThrottle::throttle_do_leak()
{
  utime_t delta, now = ceph_clock_now(cct);
  /* compute the time elapsed since the last leak */
  if (now > previous_leak)
    delta = now - previous_leak;

  previous_leak = now;

  if (delta.is_zero()) {
    return ;
  }

  /* make each bucket leak */
  for (int i = 0; i < BUCKETS_COUNT; i++)
    throttle_leak_bucket(&buckets[i], delta.to_nsec());
}

/* This function compute the wait time in ns that a leaky bucket should trigger
 *
 * @bkt: the leaky bucket we operate on
 * @ret: the resulting wait time in seconds or 0 if the operation can go through
 */
double BlockThrottle::throttle_compute_wait(LeakyBucket *bkt)
{
  if (!bkt->avg)
    return 0;

  /* the number of extra units blocking the io */
  double extra = bkt->level - bkt->max;

  if (extra <= 0)
    return 0;

  return extra / bkt->avg;
}

/* This function compute the time that must be waited while this IO
 *
 * @is_write:   true if the current IO is a write, false if it's a read
 * @ret:        time to wait(seconds)
 */
double BlockThrottle::throttle_compute_wait_for(bool is_write)
{
  BucketType to_check[2][4] = { {THROTTLE_BPS_TOTAL,
                                 THROTTLE_OPS_TOTAL,
                                 THROTTLE_BPS_READ,
                                 THROTTLE_OPS_READ},
                                {THROTTLE_BPS_TOTAL,
                                 THROTTLE_OPS_TOTAL,
                                 THROTTLE_BPS_WRITE,
                                 THROTTLE_OPS_WRITE}, };
  double wait, max_wait = 0;

  for (int i = 0; i < 4; i++) {
    BucketType index = to_check[is_write][i];
    wait = throttle_compute_wait(&buckets[index]);

    if (wait > max_wait)
      max_wait = wait;
  }

  return max_wait;
}
