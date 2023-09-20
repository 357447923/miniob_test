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
// Created by WangYunlai on 2022/07/01.
//

#include "common/log/log.h"
#include "sql/operator/project_physical_operator.h"
#include "storage/record/record.h"
#include "storage/table/table.h"

RC ProjectPhysicalOperator::open(Trx *trx)
{
  if (children_.empty()) {
    return RC::SUCCESS;
  }

  PhysicalOperator *child = children_[0].get();
  RC rc = child->open(trx);
  if (rc != RC::SUCCESS) {
    LOG_WARN("failed to open child operator: %s", strrc(rc));
    return rc;
  }

  return RC::SUCCESS;
}

RC ProjectPhysicalOperator::next()
{
  if (children_.empty()) {
    return RC::RECORD_EOF;
  }
  return children_[0]->next();
}

RC ProjectPhysicalOperator::close()
{
  if (!children_.empty()) {
    children_[0]->close();
  }
  return RC::SUCCESS;
}
Tuple *ProjectPhysicalOperator::current_tuple()
{
  if (is_none_func_ == nullptr) {
    is_none_func_ = new bool(true);
    const std::vector<AggFuncType>& types = tuple_.func_types();
    for(AggFuncType type : types) {
      if (type != FUNC_NONE) {
        *is_none_func_ = false;
        break;
      }
    }
  }
  if (!(*is_none_func_)) {
    RC rc = RC::SUCCESS;
    const std::vector<TupleCellSpec *> &speces = tuple_.speces();
    std::vector<Value> values;
    Tuple *tuple = children_[0]->current_tuple();
    int size = 1;
    for (int i = 0; i < speces.size(); i++) {
      Value tmp;
      tuple->find_cell(*speces[i], tmp);
      values.push_back(tmp);
    }
    while((rc = next()) == RC::SUCCESS) {
      size++;
      Tuple *tuple = children_[0]->current_tuple();
      for(int i = 0; i < speces.size(); i++) {
        Value tmp;
        tuple->find_cell(*speces[i], tmp);
        switch (tuple_.func_types()[i]) {
        case FUNC_MAX: {
          if (values[i].compare(tmp) < 0) {
            values[i].set_value(tmp);
          }
        };break;
        case FUNC_MIN: {
          if (values[i].compare(tmp) > 0) {
            values[i].set_value(tmp);
          }
        }break;
        case FUNC_AVG: {
          if (tmp.attr_type() == FLOATS) {
            values[i].set_float(values[i].get_float() + tmp.get_float());
          }else {
            values[i].set_int(values[i].get_int() + tmp.get_int());
          }
        }break;
        case FUNC_COUNT: break;
        default:{
          LOG_WARN("Unacceptable aggregate func");
        }break;
        }
      }
    }

    for (int i = 0; i < speces.size(); i++) {
      if (tuple_.func_types()[i] == FUNC_AVG) {
        Value &val = values[i];
        if (val.attr_type() == AttrType::INTS) {
          values[i].set_int((float)values[i].get_int() / size);
        }else {
          values[i].set_float(values[i].get_float() / size);
        }
      }else if (tuple_.func_types()[i] == FUNC_COUNT) {
        values[i].set_int(size);
      }
    }
    ValueListTuple *value_list = new ValueListTuple;
    value_list->set_cells(values);
    tuple_.set_tuple(value_list);
  } else {
    tuple_.set_tuple(children_[0]->current_tuple());
  }
  return &tuple_;
}

void ProjectPhysicalOperator::add_projection(const Field *field)
{
  // 对单表来说，展示的(alias) 字段总是字段名称，
  // 对多表查询来说，展示的alias 需要带表名字
  TupleCellSpec *spec = new TupleCellSpec(field->table_name(), field->meta()->name(), field->meta()->name());
  tuple_.add_cell_spec(spec, field->type());
}
