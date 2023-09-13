#include "sql/operator/update_logical_operator.h"

UpdateLogicalOperator::UpdateLogicalOperator(Table *table, Value value, const char *field_name) {
    table_ = table;
    value_ = std::move(value);
    char *tmp = (char *)malloc(sizeof(char) * (strlen(field_name) + 1));
    strcpy(tmp, field_name);
    field_name_ = tmp;
}

UpdateLogicalOperator::~UpdateLogicalOperator() {
    if (field_name_ != nullptr) {
        free(field_name_);
    }
}

// UpdateLogicalOperator::~UpdateLogicalOperator() {
//     if (update_map_ != nullptr) {
//         delete update_map_;
//     }
// }
