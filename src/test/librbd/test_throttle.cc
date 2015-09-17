// -*- mode:C; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
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
#include <math.h>
#include "librbd/Throttle.h"

#include "global/global_context.h"
#include "global/global_init.h"
#include "common/ceph_argparse.h"
#include "common/config.h"

#include "gtest/gtest.h"

void register_test_throttle() {
}

/* useful function */
static bool double_cmp(double x, double y)
{
    return fabsl(x - y) < 1e-6;
}

class TestThrottle : public ::testing::Test {
 public:
  BlockThrottle *throttle;

  TestThrottle(): throttle(NULL) {}
  virtual void SetUp() {
    throttle = new BlockThrottle(g_ceph_context, 0);
  }
  virtual void TearDown() {
    delete throttle;
  }
};

/* tests for single bucket operations */
TEST_F(TestThrottle, test_leak_bucket) {
  LeakyBucket bkt;
  /* set initial value */
  bkt.avg = 150;
  bkt.max = 15;
  bkt.level = 1.5;

  /* leak an op work of time */
  throttle->throttle_leak_bucket(&bkt, NANOSECONDS_PER_SECOND / 150);
  ASSERT_EQ(bkt.avg, 150);
  ASSERT_EQ(bkt.max, 15);
  ASSERT_TRUE(double_cmp(bkt.level, 0.5));

  /* leak again emptying the bucket */
  throttle->throttle_leak_bucket(&bkt, NANOSECONDS_PER_SECOND / 150);
  ASSERT_EQ(bkt.avg, 150);
  ASSERT_EQ(bkt.max, 15);
  ASSERT_TRUE(double_cmp(bkt.level, 0));

  /* check that the bucket level won't go lower */
  throttle->throttle_leak_bucket(&bkt, NANOSECONDS_PER_SECOND / 150);
  ASSERT_EQ(bkt.avg, 150);
  ASSERT_EQ(bkt.max, 15);
  ASSERT_TRUE(double_cmp(bkt.level, 0));
}

TEST_F(TestThrottle, test_compute_wait) {
  LeakyBucket bkt;
  double wait, result;

  /* no operation limit set */
  bkt.avg = 0;
  bkt.max = 15;
  bkt.level = 1.5;
  wait = throttle->throttle_compute_wait(&bkt);
  ASSERT_TRUE(!wait);

  /* zero delta */
  bkt.avg = 150;
  bkt.max = 15;
  bkt.level = 15;
  wait = throttle->throttle_compute_wait(&bkt);
  ASSERT_TRUE(!wait);

  /* below zero delta */
  bkt.avg = 150;
  bkt.max = 15;
  bkt.level = 9;
  wait = throttle->throttle_compute_wait(&bkt);
  ASSERT_TRUE(!wait);

  /* half an operation above max */
  bkt.avg = 150;
  bkt.max = 15;
  bkt.level = 15.5;
  wait = throttle->throttle_compute_wait(&bkt);
  /* time required to do half an operation */
  result = 0.5 / 150;
  ASSERT_EQ(wait, result);
}

/* function to test throttle_config and throttle_get_config */
TEST_F(TestThrottle, test_config_functions) {
  LeakyBucket buckets[BUCKETS_COUNT];

  ASSERT_FALSE(throttle->config(THROTTLE_BPS_TOTAL, -1, 0));
  ASSERT_FALSE(throttle->config(THROTTLE_BPS_TOTAL, 0, -2));

  ASSERT_TRUE(throttle->config(THROTTLE_BPS_TOTAL, 153, 0));
  ASSERT_FALSE(throttle->config(THROTTLE_BPS_READ, 56, 0));
  ASSERT_FALSE(throttle->config(THROTTLE_BPS_WRITE, 1, 0));

  ASSERT_TRUE(throttle->config(THROTTLE_OPS_READ, 69, 0));
  ASSERT_TRUE(throttle->config(THROTTLE_OPS_WRITE, 23, 0));
  ASSERT_FALSE(throttle->config(THROTTLE_OPS_TOTAL, 150, 0));

  ASSERT_TRUE(throttle->config(THROTTLE_BPS_TOTAL, 0, 1));
  ASSERT_FALSE(throttle->config(THROTTLE_BPS_READ, 0, 1));
  ASSERT_FALSE(throttle->config(THROTTLE_BPS_WRITE, 0, 1));

  ASSERT_TRUE(throttle->config(THROTTLE_OPS_READ, 0, 1));
  ASSERT_TRUE(throttle->config(THROTTLE_OPS_WRITE, 0, 1));
  ASSERT_FALSE(throttle->config(THROTTLE_OPS_TOTAL, 0, 1));

  throttle->get_config(buckets);

  ASSERT_EQ(buckets[THROTTLE_BPS_TOTAL].avg, 153);
  ASSERT_EQ(buckets[THROTTLE_BPS_READ].avg, 0);
  ASSERT_EQ(buckets[THROTTLE_BPS_WRITE].avg, 0);
  ASSERT_EQ(buckets[THROTTLE_OPS_TOTAL].avg, 0);
  ASSERT_EQ(buckets[THROTTLE_OPS_READ].avg, 69);
  ASSERT_EQ(buckets[THROTTLE_OPS_WRITE].avg, 23);
  ASSERT_EQ(buckets[THROTTLE_BPS_TOTAL].max, 1);
  ASSERT_EQ(buckets[THROTTLE_BPS_READ].max, 0);
  ASSERT_EQ(buckets[THROTTLE_BPS_WRITE].max, 0);
  ASSERT_EQ(buckets[THROTTLE_OPS_TOTAL].max, 0);
  ASSERT_EQ(buckets[THROTTLE_OPS_READ].max, 1);
  ASSERT_EQ(buckets[THROTTLE_OPS_WRITE].max, 1);
}

class FakeContext : public Context {
 public:
  FakeContext() {}
  virtual void finish(int r) {}
  virtual void complete(int r) {
  }
};

static bool do_test_accounting(bool is_ops,         /* are we testing bps or ops */
                               int size,            /* size of the operation to do */
                               double avg,          /* io limit */
                               uint64_t op_size,    /* ideal size of an io */
                               double total_result,
                               double read_result,
                               double write_result)
{
  BlockThrottle throttle(g_ceph_context, 0);
  BucketType to_test[2][3] = { { THROTTLE_BPS_TOTAL,
                                 THROTTLE_BPS_READ,
                                 THROTTLE_BPS_WRITE, },
                               { THROTTLE_OPS_TOTAL,
                                 THROTTLE_OPS_READ,
                                 THROTTLE_OPS_WRITE, } };
  BucketType index;
  for (int i = 0; i < 3; i++) {
    index = to_test[is_ops][i];
    throttle.config(index, avg, 0);
  }

  throttle.set_op_size(op_size);
  throttle.attach_context(new FakeContext(), new FakeContext());

  /* account a read */
  throttle.account(false, size);
  /* account a write */
  throttle.account(true, size);

  /* check total result */
  LeakyBucket buckets[BUCKETS_COUNT];
  throttle.get_config(buckets);

  index = to_test[is_ops][0];
  if (!double_cmp(buckets[index].level, total_result))
    return false;

  /* check read result */
  index = to_test[is_ops][1];
  if (!double_cmp(buckets[index].level, read_result))
    return false;

  /* check write result */
  index = to_test[is_ops][2];
  if (!double_cmp(buckets[index].level, write_result))
    return false;

  return true;
}

TEST_F(TestThrottle, test_accounting) {
  /* tests for bps */

  /* op of size 1 */
  ASSERT_TRUE(do_test_accounting(false,
                                 1 * 512,
                                 150,
                                 0,
                                 1024,
                                 512,
                                 512));

  /* op of size 2 */
  ASSERT_TRUE(do_test_accounting(false,
                                 2 * 512,
                                 150,
                                 0,
                                 2048,
                                 1024,
                                 1024));

  /* op of size 2 and orthogonal parameter change */
  ASSERT_TRUE(do_test_accounting(false,
                                 2 * 512,
                                 150,
                                 17,
                                 2048,
                                 1024,
                                 1024));

  /* tests for ops */

  /* op of size 1 */
  ASSERT_TRUE(do_test_accounting(true,
                                 1 * 512,
                                 150,
                                 0,
                                 2,
                                 1,
                                 1));

  /* op of size 2 */
  ASSERT_TRUE(do_test_accounting(true,
                                 2 *  512,
                                 150,
                                 0,
                                 2,
                                 1,
                                 1));

  /* jumbo op accounting fragmentation : size 64 with op size of 13 units */
  ASSERT_TRUE(do_test_accounting(true,
                                 64 * 512,
                                 150,
                                 13 * 512,
                                 (64.0 * 2) / 13,
                                 (64.0 / 13),
                                 (64.0 / 13)));

  /* same with orthogonal parameters changes */
  ASSERT_TRUE(do_test_accounting(true,
                                 64 * 512,
                                 300,
                                 13 * 512,
                                 (64.0 * 2) / 13,
                                 (64.0 / 13),
                                 (64.0 / 13)));
}

