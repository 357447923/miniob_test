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
// Created by Wangyunlai on 2022/5/22.
//

#include "sql/stmt/update_stmt.h"
#include "common/log/log.h"
#include "common/typecast.h"
#include "storage/db/db.h"
#include "storage/table/table.h"

UpdateStmt::UpdateStmt(Table *table, const Value *values, int value_amount, FilterStmt *filter_stmt, const std::string& attribute_name)
    : table_(table), values_(values), value_amount_(value_amount), filter_stmt_(filter_stmt), attribute_name_(attribute_name)
{}

RC UpdateStmt::create(Db *db, const UpdateSqlNode &update, Stmt *&stmt)
{
  // 检查db和表名合法性
  const char *table_name = update.relation_name.c_str();
  if (nullptr == db || nullptr == table_name) {
    LOG_WARN("invalid argument. db=%p, table_name=%p",
        db, table_name);
    return RC::INVALID_ARGUMENT;
  }

  // check whether the table exists
  Table *table = db->find_table(table_name);
  if (nullptr == table) {
    LOG_WARN("no such table. db=%s, table_name=%s", db->name(), table_name);
    return RC::SCHEMA_TABLE_NOT_EXIST;
  }

  // 检查更新的字段合法性
  const TableMeta &table_meta = table->table_meta();
  const FieldMeta *field_meta = table_meta.field(update.attribute_name.c_str());
  if (nullptr == field_meta) {
    LOG_WARN("no such field. table_name=%s, field=%s", table_name, update.attribute_name.c_str());
    return RC::SCHEMA_FIELD_NOT_EXIST;
  }

  // 这里由于UpdateSqlNode中仅仅只有一个value
  // 所以也仅仅支持一个字段的更新
  const AttrType value_type = update.value.attr_type();
  if (value_type == NULLS) {
    if (field_meta->not_null()) {
      LOG_ERROR("You can't put NULL to an not null field");
      return RC::SCHEMA_FIELD_NOT_NULL;
    }
  }else {
    const AttrType field_type = field_meta->type();
    if (field_type != value_type) {
      // 对update.value进行数据类型转换     // TODO 把数据类型转换做的不那么随意些，可以参考MySQL的隐式类型转换
      Value& value = const_cast<Value&>(update.value);
      RC rc = common::type_cast(value, field_meta->type());
      if (rc != RC::SUCCESS) {
        LOG_WARN("field type mismatch. table=%s, field=%s, field_type=%d, value_type=%d",
            table_name, field_meta->name(), field_type, value_type);
        return RC::SCHEMA_FIELD_TYPE_MISMATCH;
      }
    }
  }
  
  std::unordered_map<std::string, Table *> table_map;
  table_map.insert(std::pair<std::string, Table *>(update.relation_name, table));
  // 构造过滤语句
  FilterStmt *filter_stmt = nullptr;
  RC rc = FilterStmt::create(
    db, table, &table_map, update.conditions.data(), static_cast<int>(update.conditions.size()), filter_stmt);
  if (rc != RC::SUCCESS) {
    LOG_WARN("failed to create filter statement. rc=%d:%s", rc, strrc(rc));
    return rc;
  }
  // count为1是因为update中只有一个value
  stmt = new UpdateStmt(table, &update.value, 1, filter_stmt, update.attribute_name);
  return RC::SUCCESS;
}

