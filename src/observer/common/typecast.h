#include "common/date.h"
#include "common/log/log.h"
#include "sql/parser/value.h"
#include "common/lang/string.h"
#include "common/rc.h"
namespace common
{
    typedef RC (*type_cast_t) (Value& value, AttrType target_type);

    static const type_cast_t type_cast_table[] = {
        [UNDEFINED] = nullptr,
        [CHARS] = str_to_target,
        [INTS] = ints_to_target,
        [FLOATS] = floats_to_target,
        [DATES] = dates_to_target,
        [NULLS] = nullptr,
        [BOOLEANS] = nullptr,
    };

    inline static RC type_cast(Value& value, AttrType target_type) {
        type_cast_t func = type_cast_table[value.attr_type()];
        if (func == nullptr) {
            LOG_ERROR("type:'%s' can't be cast to another type", attr_type_to_string(value.attr_type()));
            return RC::TYPE_CAST_ERROR;
        }
        return func(value, target_type);
    }
};