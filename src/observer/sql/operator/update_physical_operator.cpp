#include "sql/operator/update_physical_operator.h"
#include "storage/trx/trx.h"
#include "storage/record/record.h"

UpdatePhysicalOperator::UpdatePhysicalOperator(Table *table, Value& value, const char * field_name) {
  table_ = table;
  value_ = value;
  char *tmp = (char *)malloc(sizeof(char) * (strlen(field_name) + 1));
  strcpy(tmp, field_name);
  field_name_ = tmp;
}

UpdatePhysicalOperator::~UpdatePhysicalOperator() {
  if (field_name_ != nullptr) {
    delete field_name_;
  }
}

RC UpdatePhysicalOperator::open(Trx *trx) {
  if (children_.empty()) {
    return RC::SUCCESS;
  }

  std::unique_ptr<PhysicalOperator> &child = children_[0];
  RC rc = child->open(trx);
  if (rc != RC::SUCCESS) {
    LOG_WARN("failed to open child operator: %s", strrc(rc));
    return rc;
  }

  trx_ = trx;

  return RC::SUCCESS;
}

RC UpdatePhysicalOperator::close() {
    if (!children_.empty()) {
        children_[0]->close();
    }
    return RC::SUCCESS;
}

RC UpdatePhysicalOperator::next() {
  RC rc = RC::SUCCESS;
  if (children_.empty()) {
    return RC::RECORD_EOF;
  }

  PhysicalOperator *child = children_[0].get();
  while (RC::SUCCESS == (rc = child->next())) {
    Tuple *tuple = child->current_tuple();
    if (nullptr == tuple) {
      LOG_WARN("failed to get current record: %s", strrc(rc));
      return rc;
    }
    
    RowTuple *row_tuple = static_cast<RowTuple *>(tuple);
    Record &record = row_tuple->record();
    const FieldMeta *field = table_->table_meta().field(field_name_);
    int offset = field->offset();
    rc = trx_->update_record(table_, record, offset, value_);
    if (rc != RC::SUCCESS) {
      LOG_WARN("failed to delete record: %s", strrc(rc));
      return rc;
    }
  }

  return RC::RECORD_EOF;
}