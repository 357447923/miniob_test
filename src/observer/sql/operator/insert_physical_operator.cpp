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
// Created by WangYunlai on 2021/6/9.
//

#include "sql/operator/insert_physical_operator.h"
#include "sql/stmt/insert_stmt.h"
#include "storage/table/table.h"
#include "storage/trx/trx.h"

using namespace std;

InsertPhysicalOperator::InsertPhysicalOperator(Table *table, const vector<vector<Value>> *values)
    : table_(table), values_(values)
{}

RC InsertPhysicalOperator::open(Trx *trx)
{
  vector<Record> records;
  int count = static_cast<int>(values_->size());

  RC rc = RC::SUCCESS;
  // 初始化所有的记录
  for (int i = 0; i < count; i++) {
    Record record;
    rc = table_->make_record(values_->operator[](i).size(), values_->operator[](i).data(), record);
    if (rc != RC::SUCCESS) {
      LOG_WARN("failed to make record. rc=%s", strrc(rc));
      return rc;
    }
    records.push_back(record);
  }
  // 插入所有的记录，success字段用来做错误回退
  int success;
  for (success = 0; success < count; ++success) {
    rc = trx->insert_record(table_, records[success]);
    if (rc != RC::SUCCESS) {
      LOG_WARN("failed to insert record by transaction. rc=%s", strrc(rc));
      goto failed_to_insert;
    }
  }
  return rc;

failed_to_insert:
  // 错误回退, 解决回退失败 TODO
  for (int i = 0; i < success; ++i){
    rc = trx->delete_record(table_, records[i]);
    if (rc != RC::SUCCESS) {
      LOG_ERROR("Failed to rollback record by transaction. rc=%s, no=%d", strrc(rc), i);
    }
  }
  return rc;
}

RC InsertPhysicalOperator::next()
{
  return RC::RECORD_EOF;
}

RC InsertPhysicalOperator::close()
{
  return RC::SUCCESS;
}
