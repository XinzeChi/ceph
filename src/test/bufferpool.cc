// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab
/*
 * Ceph - scalable distributed file system
 *
 * Copyright (C) 2013 Cloudwatt <libre.licensing@cloudwatt.com>
 *
 * Author: Xinze Chi <xmdxcxz@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Library Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Library Public License for more details.
 *
 */

#include <stdio.h>
#include "include/buffer.h"
#include "common/buffer.cc"
#include "gtest/gtest.h"

TEST(BufferPool, test)
{
  const int32_t raw_size = 4096;
  ceph::buffer_raw_pools *pool = ceph::pool;
  {
    buffer::raw *ret = pool->get_from_pool(raw_size, buffer::ALIGN); 
    EXPECT_EQ(NULL, ret);
  }

  {
    bufferptr ptr = buffer::create_page_aligned(raw_size);
    EXPECT_TRUE(ptr.get_raw() != NULL);
  }
  
  {
    buffer::raw *raw = pool->get_from_pool(raw_size, buffer::ALIGN);
    EXPECT_TRUE(raw != NULL);
    bufferptr ptr = raw;
    raw = pool->get_from_pool(raw_size, buffer::ALIGN);
    EXPECT_TRUE(raw == NULL);
  }

  {
    bufferptr ptr = pool->get_from_pool(raw_size, buffer::ALIGN);
    EXPECT_TRUE(ptr.get_raw() != NULL);
  }

  {
    bufferptr ptr[3];
    for (int i = 0; i < 3; i++) {
      ptr[i] = buffer::create(raw_size + 1);
    }
  }

  {
    for (int i = 0; i < 3; i++) {
      buffer::raw *raw = pool->get_from_pool(raw_size + 1, buffer::NORMAL);
      EXPECT_TRUE(raw != NULL);
    }
    buffer::raw *raw = pool->get_from_pool(raw_size + 1, buffer::NORMAL);
    EXPECT_TRUE(raw == NULL);
    raw = pool->get_from_pool(raw_size + 1, buffer::ALIGN);
    EXPECT_TRUE(raw == NULL);
  }
  delete pool;
}

// Local Variables:
// compile-command: "cd .. ; make unittest_bufferpool && ./unittest_bufferpool # --gtest_filter=*.* --log-to-stderr=true"
// End:

