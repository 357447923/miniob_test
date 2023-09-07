#ifndef MINIOB_DROP_TABLE_EXCUTOR_H
#define MINIOB_DROP_TABLE_EXCUTOR_H

#include "common/rc.h"

class SQLStageEvent;
class Table;
class SqlResult;

/**
 * @brief 导入数据的执行器
 * @ingroup Executor
 */
class DropTableExecutor
{
public:
  DropTableExecutor() = default;
  virtual ~DropTableExecutor() = default;

  RC execute(SQLStageEvent *sql_event);
};


#endif