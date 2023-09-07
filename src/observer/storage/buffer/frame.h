/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
         http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

//
// Created by lianyu on 2022/10/29.
//

#pragma once

#include <pthread.h>
#include <string.h>
#include <string>
#include <mutex>
#include <set>
#include <atomic>

#include "storage/buffer/page.h"
#include "common/log/log.h"
#include "common/lang/mutex.h"
#include "common/types.h"

/**
 * @brief 页帧标识符
 * @ingroup BufferPool
 */
class FrameId 
{
public:
  FrameId(int file_desc, PageNum page_num);
  bool    equal_to(const FrameId &other) const;
  bool    operator==(const FrameId &other) const;
  size_t  hash() const;
  int     file_desc() const;
  PageNum page_num() const;

  friend std::string to_string(const FrameId &frame_id);
private:
  int     file_desc_;
  PageNum page_num_;
};

/**
 * @brief 页帧
 * @ingroup BufferPool
 * @details 页帧是磁盘文件在内存中的表示。磁盘文件按照页面来操作，操作之前先映射到内存中，
 * 将磁盘数据读取到内存中，也就是页帧。
 * 
 * 当某个页面被淘汰时，如果有些内容曾经变更过，那么就需要将这些内容刷新到磁盘上。这里有
 * 一个dirty标识，用来标识页面是否被修改过。
 * 
 * 为了防止在使用过程中页面被淘汰，这里使用了pin count，当页面被使用时，pin count会增加，
 * 当页面不再使用时，pin count会减少。当pin count为0时，页面可以被淘汰。
 */
class Frame
{
public:
  ~Frame()
  {
    // LOG_DEBUG("deallocate frame. this=%p, lbt=%s", this, common::lbt());
  }

  /**
   * @brief reinit 和 reset 在 MemPoolSimple 中使用
   * @details 在 MemPoolSimple 分配和释放一个Frame对象时，不会调用构造函数和析构函数，
   * 而是调用reinit和reset。
   */
  void reinit()
  {}
  void reset()
  {}
  
  void clear_page()
  {
    memset(&page_, 0, sizeof(page_));
  }

  int     file_desc() const { return file_desc_; }
  void    set_file_desc(int fd) { file_desc_ = fd; }
  Page &  page() { return page_; }
  PageNum page_num() const { return page_.page_num; }
  void    set_page_num(PageNum page_num) { page_.page_num = page_num; }
  FrameId frame_id() const { return FrameId(file_desc_, page_.page_num); }
  LSN     lsn() const { return page_.lsn; }
  void    set_lsn(LSN lsn) { page_.lsn = lsn; }

  /// 刷新访问时间 TODO touch is better?
  void access();

  /**
   * @brief 标记指定页面为“脏”页。如果修改了页面的内容，则应调用此函数，
   * 以便该页面被淘汰出缓冲区时系统将新的页面数据写入磁盘文件
   */
  void mark_dirty() { dirty_ = true; }
  void clear_dirty() { dirty_ = false; }
  bool dirty() const { return dirty_; }

  char *data() { return page_.data; }

  bool can_purge() { return pin_count_.load() == 0; }

  /**
   * @brief 给当前页帧增加引用计数
   * pin通常都会加着frame manager锁来访问
   */
  void pin();

  /**
   * @brief 释放一个当前页帧的引用计数
   * 与pin对应，但是通常不会加着frame manager的锁来访问
   */
  int  unpin();
  int  pin_count() const { return pin_count_.load(); }

  void write_latch();
  void write_latch(intptr_t xid);

  void write_unlatch();
  void write_unlatch(intptr_t xid);
  
  void read_latch();
  void read_latch(intptr_t xid);
  bool try_read_latch();

  void read_unlatch();
  void read_unlatch(intptr_t xid);

  friend std::string to_string(const Frame &frame);

private:
  friend class  BufferPool;

  bool              dirty_     = false;
  std::atomic<int>  pin_count_{0};
  unsigned long     acc_time_  = 0;
  int               file_desc_ = -1;
  Page              page_;

  /// 在非并发编译时，加锁解锁动作将什么都不做
  common::RecursiveSharedMutex     lock_;

  /// 使用一些手段来做测试，提前检测出头疼的死锁问题
  /// 如果编译时没有增加调试选项，这些代码什么都不做
  common::DebugMutex  debug_lock_;
  intptr_t            write_locker_ = 0;
  int                 write_recursive_count_ = 0;
  std::unordered_map<intptr_t, int>  read_lockers_;
};

