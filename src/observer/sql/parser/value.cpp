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
// Created by WangYunlai on 2023/06/28.
//

#include <sstream>
#include <string>
#include "sql/parser/value.h"
#include "storage/field/field.h"
#include "common/log/log.h"
#include "common/lang/comparator.h"
#include "common/lang/string.h"
#include "common/date.h"
#include "common/typecast.h"

const char *ATTR_TYPE_NAME[] = {
  [UNDEFINED] = "undefined", 
  [CHARS] = "chars", 
  [INTS] = "ints", 
  [FLOATS] = "floats", 
  [DATES] = "dates", 
  [NULLS] = "nulls",
  [BOOLEANS] = "booleans"
};

const char *attr_type_to_string(AttrType type)
{
  if (type >= UNDEFINED && type <= BOOLEANS) {
    return ATTR_TYPE_NAME[type];
  }
  return "unknown";
}
AttrType attr_type_from_string(const char *s)
{
  for (unsigned int i = 0; i < sizeof(ATTR_TYPE_NAME) / sizeof(ATTR_TYPE_NAME[0]); i++) {
    if (0 == strcmp(ATTR_TYPE_NAME[i], s)) {
      return (AttrType)i;
    }
  }
  return UNDEFINED;
}

/**
 * 下面是几种类型转换到对应类型的函数
 */

RC str_to_target(Value& value, AttrType target) {
  // 类型相同时直接返回
  if (target == CHARS) {
    return RC::SUCCESS;
  }
  // 不是以下几种类型，直接返回错误
  if (target != INTS && target != FLOATS && target != DATES) {
    LOG_ERROR("cast to int error, because str can't be convert to %s", attr_type_to_string(target));
    return RC::TYPE_CAST_ERROR;
  }
  // 检验是否可以被解析
  int len = value.length();
  if (len <= 0) {
    LOG_WARN("cast to int error, because strlen=0");
    return RC::TYPE_CAST_ERROR;
  }
  const char *str = value.get_string().c_str();
  if (str[0] < '0' || str[0] > '9') {
    LOG_WARN("cast to int error, because str:'%s' contains non-number", str);
    return RC::TYPE_CAST_ERROR;
  }

  // 为不同数据类型提供不同的转换方法
  switch (target) {
    case INTS: {
      int number = atoi(str);
      if (number == 0 && str[0] != '0') {
        LOG_WARN("cast to int error, because str:'%s' can't be convert to integer", str);
        return RC::TYPE_CAST_ERROR;
      }
      value.set_int(number);
    }break;

    case FLOATS: {
      float number = atof(str);
      if (number == 0 && str[0] != '0') {
        LOG_WARN("cast to int error, because str:'%s' can't be convert to float", str);
        return RC::TYPE_CAST_ERROR;
      }
      value.set_float(number);
    }break;

    case DATES: {
      int date = -1;
      if (str_to_date(str, date) != RC::SUCCESS) {
        LOG_WARN("cast to date error, because str:'%s' can't be convert to date", str);
        return RC::TYPE_CAST_ERROR;
      }
      value.set_date(date);
    }break;
  }
  return RC::SUCCESS;
}

RC ints_to_target(Value& value, AttrType target) {
  if (target == INTS) {
    return RC::SUCCESS;
  }

  if (target != FLOATS && target != CHARS) {
    LOG_ERROR("Type: ints can't cast to %s", attr_type_to_string(target));
    return RC::TYPE_CAST_ERROR;
  }

  switch (target) {
    case FLOATS:
      value.set_float(value.get_int());
      break;
    case CHARS: {
      std::string str = std::to_string(value.get_int());
      value.set_string(str.c_str(), str.length());
    }break;
  }

  return RC::SUCCESS;
}

RC floats_to_target(Value& value, AttrType target) {
  if (target == FLOATS) {
    return RC::SUCCESS;
  }

  if (target != INTS && target != CHARS) {
    LOG_ERROR("Type: floats can't cast to %s", attr_type_to_string(target));
    return RC::TYPE_CAST_ERROR;
  }

  switch (target) {
  case INTS:
    value.set_int((int)value.get_float());
    break;
  case CHARS: {
    std::string str = std::to_string(value.get_float());
    value.set_string(str.c_str(), str.length());
  }break;
  }
  return RC::SUCCESS;
}

RC dates_to_target(Value& value, AttrType target) {
  if (target == DATES) {
    return RC::SUCCESS;
  }
  if (target != CHARS) {
    LOG_ERROR("date can't cast to %s", attr_type_to_string(target));
    return RC::TYPE_CAST_ERROR;
  }
  std::string str = date_to_str(value.get_date());
  value.set_string(str.c_str(), str.length());

  return RC::SUCCESS;
}

Value::Value(int val)
{
  set_int(val);
}

Value::Value(float val)
{
  set_float(val);
}

Value::Value(bool val)
{
  set_boolean(val);
}

Value::Value(const char *s, int len /*= 0*/)
{
  set_string(s, len);
}

void Value::set_data(char *data, int length)
{
  switch (attr_type_) {
    case CHARS: {
      set_string(data, length);
    } break;
    case NULLS: {
      set_null();
    }break;
    case INTS: case DATES: {
      num_value_.int_value_ = *(int *)data;
      length_ = length;
    } break;
    case FLOATS: {
      num_value_.float_value_ = *(float *)data;
      length_ = length;
    } break;
    case BOOLEANS: {
      num_value_.bool_value_ = *(int *)data != 0;
      length_ = length;
    } break;
    default: {
      LOG_WARN("unknown data type: %d", attr_type_);
    } break;
  }
}
void Value::set_int(int val)
{
  attr_type_ = INTS;
  num_value_.int_value_ = val;
  length_ = sizeof(val);
}

void Value::set_float(float val)
{
  attr_type_ = FLOATS;
  num_value_.float_value_ = val;
  length_ = sizeof(val);
}
void Value::set_boolean(bool val)
{
  attr_type_ = BOOLEANS;
  num_value_.bool_value_ = val;
  length_ = sizeof(val);
}
void Value::set_null() {
  attr_type_ = NULLS;
  length_ = 0;
}

void Value::set_string(const char *s, int len /*= 0*/)
{
  attr_type_ = CHARS;
  if (len > 0) {
    len = strnlen(s, len);
    str_value_.assign(s, len);
  } else {
    str_value_.assign(s);
  }
  length_ = str_value_.length();
}

void Value::set_date(int32_t date) {
  attr_type_ = DATES;
  num_value_.int_value_ = date;
  length_ = sizeof(date);
}

void Value::set_value(const Value &value)
{
  switch (value.attr_type_) {
    case INTS: {
      set_int(value.get_int());
    } break;
    case FLOATS: {
      set_float(value.get_float());
    } break;
    case NULLS: {
      set_null();
    }break;
    case CHARS: {
      set_string(value.get_string().c_str());
    } break;
    case BOOLEANS: {
      set_boolean(value.get_boolean());
    } break;
    case DATES: {
      set_date(value.get_int());
    }
    case UNDEFINED: {
      ASSERT(false, "got an invalid value type");
    } break;
  }
}

const char *Value::data() const
{
  switch (attr_type_) {
    case CHARS: {
      return str_value_.c_str();
    } break;
    default: {
      return (const char *)&num_value_;
    } break;
  }
}

std::string Value::to_string() const
{
  std::stringstream os;
  switch (attr_type_) {
    case INTS: {
      os << num_value_.int_value_;
    } break;
    case FLOATS: {
      os << common::double_to_str(num_value_.float_value_);
    } break;
    case BOOLEANS: {
      os << num_value_.bool_value_;
    } break;
    case NULLS: {
      os << "NULL";
    }break;
    case CHARS: {
      os << str_value_;
    } break;
    case DATES: {
      int32_t date = num_value_.int_value_;
      os << date_to_str(date);
    }break;
    default: {
      LOG_WARN("unsupported attr type: %d", attr_type_);
    } break;
  }
  return os.str();
}

int Value::compare(const Value &other) const
{
  if (this->attr_type_ == other.attr_type_) {
    switch (this->attr_type_) {
      case INTS: case DATES: {
        return common::compare_int((void *)&this->num_value_.int_value_, (void *)&other.num_value_.int_value_);
      } break;
      case FLOATS: {
        return common::compare_float((void *)&this->num_value_.float_value_, (void *)&other.num_value_.float_value_);
      } break;
      case CHARS: {
        return common::compare_string((void *)this->str_value_.c_str(),
            this->str_value_.length(),
            (void *)other.str_value_.c_str(),
            other.str_value_.length());
      } break;
      case BOOLEANS: {
        return common::compare_int((void *)&this->num_value_.bool_value_, (void *)&other.num_value_.bool_value_);
      }
      case NULLS: {
        return -1;
      }
      default: {
        LOG_WARN("unsupported type: %d", this->attr_type_);
      }
    }
  } else if (this->attr_type_ == NULLS || other.attr_type_ == NULLS) {
    // 只要有一个是NULL，比较结果一定不相等
    return -1;
  } else if (this->attr_type_ == INTS && other.attr_type_ == FLOATS) {
    float this_data = this->num_value_.int_value_;
    return common::compare_float((void *)&this_data, (void *)&other.num_value_.float_value_);
  } else if (this->attr_type_ == FLOATS && other.attr_type_ == INTS) {
    float other_data = other.num_value_.int_value_;
    return common::compare_float((void *)&this->num_value_.float_value_, (void *)&other_data);
  }
  // TODO 可能可以考虑增加字符串与日期的比较
  LOG_WARN("not supported");
  return -1;  // TODO return rc?
}

int Value::get_int() const
{
  switch (attr_type_) {
    case CHARS: {
      try {
        return (int)(std::stol(str_value_));
      } catch (std::exception const &ex) {
        LOG_TRACE("failed to convert string to number. s=%s, ex=%s", str_value_.c_str(), ex.what());
        return 0;
      }
    }
    case INTS: case DATES: {
      return num_value_.int_value_;
    }
    case FLOATS: {
      return (int)(num_value_.float_value_);
    }
    case BOOLEANS: {
      return (int)(num_value_.bool_value_);
    }
    default: {
      LOG_WARN("unknown data type. type=%d", attr_type_);
      return 0;
    }
  }
  return 0;
}

float Value::get_float() const
{
  switch (attr_type_) {
    case CHARS: {
      try {
        return std::stof(str_value_);
      } catch (std::exception const &ex) {
        LOG_TRACE("failed to convert string to float. s=%s, ex=%s", str_value_.c_str(), ex.what());
        return 0.0;
      }
    } break;
    case INTS: {
      return float(num_value_.int_value_);
    } break;
    case FLOATS: {
      return num_value_.float_value_;
    } break;
    case BOOLEANS: {
      return float(num_value_.bool_value_);
    } break;
    default: {
      LOG_WARN("unknown data type. type=%d", attr_type_);
      return 0;
    }
  }
  return 0;
}

std::string Value::get_string() const
{
  return this->to_string();
}

bool Value::get_boolean() const
{
  switch (attr_type_) {
    case CHARS: {
      try {
        float val = std::stof(str_value_);
        if (val >= EPSILON || val <= -EPSILON) {
          return true;
        }

        int int_val = std::stol(str_value_);
        if (int_val != 0) {
          return true;
        }

        return !str_value_.empty();
      } catch (std::exception const &ex) {
        LOG_TRACE("failed to convert string to float or integer. s=%s, ex=%s", str_value_.c_str(), ex.what());
        return !str_value_.empty();
      }
    } break;
    case INTS: {
      return num_value_.int_value_ != 0;
    } break;
    case FLOATS: {
      float val = num_value_.float_value_;
      return val >= EPSILON || val <= -EPSILON;
    } break;
    case BOOLEANS: {
      return num_value_.bool_value_;
    } break;
    default: {
      LOG_WARN("unknown data type. type=%d", attr_type_);
      return false;
    }
  }
  return false;
}

int32_t Value::get_date() const {
  switch (attr_type_)
  {
  case INTS: case DATES: {
    return num_value_.int_value_;
  }break;
  case CHARS: {
    int32_t date = -1;
    RC rc = str_to_date(str_value_.c_str(), date);
    if (rc != RC::SUCCESS) {
      LOG_WARN("Failed to convert data type. str=%s, target_type=%s", str_value_, "DATES");
    }
    return date;
  }break;
  default: {
      LOG_WARN("unknown data type. type=%d", attr_type_);
      return 0;
  }break;
  }
}