#include "sql/operator/update_logical_operator.h"

UpdateLogicalOperator::UpdateLogicalOperator(Table *table, Value value) : table_(table), value_(value)
{}

// UpdateLogicalOperator::~UpdateLogicalOperator() {
//     if (update_map_ != nullptr) {
//         delete update_map_;
//     }
// }
