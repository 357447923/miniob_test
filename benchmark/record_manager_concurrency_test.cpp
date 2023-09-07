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
// Created by Wangyunlai on 2023/05/04
//

#include <inttypes.h>
#include <random>
#include <stdexcept>
#include <benchmark/benchmark.h>

#include "storage/record/record_manager.h"
#include "storage/buffer/disk_buffer_pool.h"
#include "storage/common/condition_filter.h"
#include "storage/trx/vacuous_trx.h"
#include "common/log/log.h"
#include "integer_generator.h"

using namespace std;
using namespace common;
using namespace benchmark;

once_flag         init_bpm_flag;
BufferPoolManager bpm{512};

struct Stat
{
  int64_t insert_success_count = 0;
  int64_t insert_other_count   = 0;

  int64_t delete_success_count = 0;
  int64_t not_exist_count      = 0;
  int64_t delete_other_count   = 0;

  int64_t scan_success_count     = 0;
  int64_t scan_open_failed_count = 0;
  int64_t mismatch_count         = 0;
  int64_t scan_other_count       = 0;
};

struct TestRecord
{
  int32_t int_fields[15];
};

class TestConditionFilter : public ConditionFilter
{
public:
  TestConditionFilter(int32_t begin, int32_t end) : begin_(begin), end_(end) {}

  bool filter(const Record &rec) const override
  {
    const char *data  = rec.data();
    int32_t     value = *(int32_t *)data;
    return value >= begin_ && value <= end_;
  }

private:
  int32_t begin_;
  int32_t end_;
};

class BenchmarkBase : public Fixture
{
public:
  BenchmarkBase() {}

  virtual ~BenchmarkBase() { BufferPoolManager::set_instance(nullptr); }

  virtual string Name() const = 0;

  string record_filename() const { return this->Name() + ".record"; }

  virtual void SetUp(const State &state)
  {
    if (0 != state.thread_index()) {
      return;
    }

    string log_name        = this->Name() + ".log";
    string record_filename = this->record_filename();
    LoggerFactory::init_default(log_name.c_str(), LOG_LEVEL_TRACE);

    std::call_once(init_bpm_flag, []() { BufferPoolManager::set_instance(&bpm); });

    ::remove(record_filename.c_str());

    RC rc = bpm.create_file(record_filename.c_str());
    if (rc != RC::SUCCESS) {
      LOG_WARN("failed to create record buffer pool file. filename=%s, rc=%s", record_filename.c_str(), strrc(rc));
      throw runtime_error("failed to create record buffer pool file.");
    }

    rc = bpm.open_file(record_filename.c_str(), buffer_pool_);
    if (rc != RC::SUCCESS) {
      LOG_WARN("failed to open record file. filename=%s, rc=%s", record_filename.c_str(), strrc(rc));
      throw runtime_error("failed to open record file");
    }

    rc = handler_.init(buffer_pool_);
    if (rc != RC::SUCCESS) {
      LOG_WARN("failed to init record file handler. rc=%s", strrc(rc));
      throw runtime_error("failed to init record file handler");
    }
    LOG_INFO(
        "test %s setup done. threads=%d, thread index=%d", this->Name().c_str(), state.threads(), state.thread_index());
  }

  virtual void TearDown(const State &state)
  {
    if (0 != state.thread_index()) {
      return;
    }

    handler_.close();
    bpm.close_file(this->record_filename().c_str());
    buffer_pool_ = nullptr;
    LOG_INFO("test %s teardown done. threads=%d, thread index=%d",
        this->Name().c_str(),
        state.threads(),
        state.thread_index());
  }

  void FillUp(int32_t min, int32_t max, vector<RID> &rids)
  {
    rids.reserve(max - min);
    RID             rid;
    TestRecord      record;
    vector<int32_t> record_values;
    record_values.reserve(max - min);
    for (int32_t value = min; value < max; ++value) {
      record_values.push_back(value);
    }

    random_device rd;
    mt19937       random_generator(rd());
    shuffle(record_values.begin(), record_values.end(), random_generator);

    for (int32_t record_value : record_values) {
      record.int_fields[0]   = record_value;
      [[maybe_unused]] RC rc = handler_.insert_record(reinterpret_cast<const char *>(&record), sizeof(record), &rid);
      ASSERT(rc == RC::SUCCESS, "failed to insert record into record file. record value=%" PRIu32, record_value);
      rids.push_back(rid);
    }

    LOG_INFO("fill up done. min=%" PRIu32 ", max=%" PRIu32 ", distance=%" PRIu32, min, max, (max - min));
  }

  uint32_t GetRangeMax(const State &state) const
  {
    uint32_t max = static_cast<uint32_t>(state.range(0) * 3);
    if (max <= 0) {
      max = (1 << 31);
    }
    return max;
  }

  void Insert(int32_t value, Stat &stat, RID &rid)
  {
    TestRecord record;
    record.int_fields[0] = value;

    RC rc = handler_.insert_record(reinterpret_cast<const char *>(&record), sizeof(record), &rid);
    switch (rc) {
      case RC::SUCCESS: {
        stat.insert_success_count++;
      } break;
      default: {
        stat.insert_other_count++;
      } break;
    }
  }

  void Delete(const RID &rid, Stat &stat)
  {
    RC rc = handler_.delete_record(&rid);
    switch (rc) {
      case RC::SUCCESS: {
        stat.delete_success_count++;
      } break;
      case RC::RECORD_NOT_EXIST: {
        stat.not_exist_count++;
      } break;
      default: {
        stat.delete_other_count++;
      } break;
    }
  }

  void Scan(int32_t begin, int32_t end, Stat &stat)
  {
    TestConditionFilter condition_filter(begin, end);
    RecordFileScanner   scanner;
    VacuousTrx          trx;
    RC rc = scanner.open_scan(nullptr /*table*/, *buffer_pool_, &trx, true /*readonly*/, &condition_filter);
    if (rc != RC::SUCCESS) {
      stat.scan_open_failed_count++;
    } else {
      Record  record;
      int32_t count = 0;
      while (scanner.has_next()) {
        rc = scanner.next(record);
        ASSERT(rc == RC::SUCCESS, "failed to get record, rc=%s", strrc(rc));
        count++;
      }

      if (rc != RC::SUCCESS) {
        stat.scan_other_count++;
      } else if (count != (end - begin + 1)) {
        stat.mismatch_count++;
      } else {
        stat.scan_success_count++;
      }

      scanner.close_scan();
    }
  }

protected:
  DiskBufferPool   *buffer_pool_ = nullptr;
  RecordFileHandler handler_;
};

////////////////////////////////////////////////////////////////////////////////

struct InsertionBenchmark : public BenchmarkBase
{
  string Name() const override { return "insertion"; }
};

BENCHMARK_DEFINE_F(InsertionBenchmark, Insertion)(State &state)
{
  IntegerGenerator generator(1, 1 << 31);
  Stat             stat;

  RID rid;
  for (auto _ : state) {
    Insert(generator.next(), stat, rid);
  }

  state.counters["success"] = Counter(stat.insert_success_count, Counter::kIsRate);
  state.counters["other"]   = Counter(stat.insert_other_count, Counter::kIsRate);
}

BENCHMARK_REGISTER_F(InsertionBenchmark, Insertion)->Threads(10);

////////////////////////////////////////////////////////////////////////////////

class DeletionBenchmark : public BenchmarkBase
{
public:
  string Name() const override { return "deletion"; }

  void SetUp(const State &state) override
  {
    if (0 != state.thread_index()) {
      return;
    }

    BenchmarkBase::SetUp(state);

    uint32_t max = GetRangeMax(state);
    ASSERT(max > 0, "invalid argument count. %ld", state.range(0));
    FillUp(0, max, rids_);
  }

protected:
  vector<RID> rids_;
};

BENCHMARK_DEFINE_F(DeletionBenchmark, Deletion)(State &state)
{
  IntegerGenerator generator(0, static_cast<int>(rids_.size()));
  Stat             stat;

  for (auto _ : state) {
    int32_t value = generator.next();
    RID     rid   = rids_[value];
    Delete(rid, stat);
  }

  state.counters["success"]   = Counter(stat.delete_success_count, Counter::kIsRate);
  state.counters["not_exist"] = Counter(stat.not_exist_count, Counter::kIsRate);
  state.counters["other"]     = Counter(stat.delete_other_count, Counter::kIsRate);
}

BENCHMARK_REGISTER_F(DeletionBenchmark, Deletion)->Threads(10)->Arg(4 * 10000);

////////////////////////////////////////////////////////////////////////////////

class ScanBenchmark : public BenchmarkBase
{
public:
  string Name() const override { return "scan"; }

  void SetUp(const State &state) override
  {
    if (0 != state.thread_index()) {
      return;
    }

    BenchmarkBase::SetUp(state);

    int32_t max = state.range(0) * 3;
    ASSERT(max > 0, "invalid argument count. %ld", state.range(0));
    vector<RID> rids;
    FillUp(0, max, rids);
  }
};

BENCHMARK_DEFINE_F(ScanBenchmark, Scan)(State &state)
{
  int              max_range_size = 100;
  uint32_t         max            = GetRangeMax(state);
  IntegerGenerator begin_generator(1, max - max_range_size);
  IntegerGenerator range_generator(1, max_range_size);
  Stat             stat;

  for (auto _ : state) {
    int32_t begin = begin_generator.next();
    int32_t end   = begin + range_generator.next();
    Scan(begin, end, stat);
  }

  state.counters["success"]               = Counter(stat.scan_success_count, Counter::kIsRate);
  state.counters["open_failed_count"]     = Counter(stat.scan_open_failed_count, Counter::kIsRate);
  state.counters["mismatch_number_count"] = Counter(stat.mismatch_count, Counter::kIsRate);
  state.counters["other"]                 = Counter(stat.scan_other_count, Counter::kIsRate);
}

BENCHMARK_REGISTER_F(ScanBenchmark, Scan)->Threads(10)->Arg(4 * 10000);

////////////////////////////////////////////////////////////////////////////////

struct MixtureBenchmark : public BenchmarkBase
{
  string Name() const override { return "mixture"; }
};

BENCHMARK_DEFINE_F(MixtureBenchmark, Mixture)(State &state)
{
  pair<int32_t, int32_t> data_range{0, GetRangeMax(state)};
  pair<int32_t, int32_t> scan_range{1, 100};

  IntegerGenerator data_generator(data_range.first, data_range.second);
  IntegerGenerator scan_range_generator(scan_range.first, scan_range.second);
  IntegerGenerator operation_generator(0, 2);

  Stat stat;

  vector<RID> rids;
  for (auto _ : state) {
    int operation_type = operation_generator.next();
    switch (operation_type) {
      case 0: {  // insert
        int32_t value = data_generator.next();
        RID     rid;
        Insert(value, stat, rid);
        if (rids.size() < 1000000) {
          rids.push_back(rid);
        }
      } break;
      case 1: {  // delete
        int32_t index = data_generator.next();
        if (!rids.empty()) {
          index %= rids.size();
          RID rid = rids[index];
          rids.erase(rids.begin() + index);
          Delete(rid, stat);
        }
      } break;
      case 2: {  // scan
        int32_t begin = data_generator.next();
        int32_t end   = begin + scan_range_generator.next();
        Scan(begin, end, stat);
      } break;
      default: {
        ASSERT(false, "should not happen. operation=%ld", operation_type);
      }
    }
  }

  state.counters.insert({{"insert_success", Counter(stat.insert_success_count, Counter::kIsRate)},
      {"insert_other", Counter(stat.insert_other_count, Counter::kIsRate)},
      {"delete_success", Counter(stat.delete_success_count, Counter::kIsRate)},
      {"delete_other", Counter(stat.delete_other_count, Counter::kIsRate)},
      {"delete_not_exist", Counter(stat.not_exist_count, Counter::kIsRate)},
      {"scan_success", Counter(stat.scan_success_count, Counter::kIsRate)},
      {"scan_other", Counter(stat.scan_other_count, Counter::kIsRate)},
      {"scan_mismatch", Counter(stat.mismatch_count, Counter::kIsRate)},
      {"scan_open_failed", Counter(stat.scan_open_failed_count, Counter::kIsRate)}});
}

BENCHMARK_REGISTER_F(MixtureBenchmark, Mixture)->Threads(10)->Arg(4 * 10000);

////////////////////////////////////////////////////////////////////////////////

BENCHMARK_MAIN();
