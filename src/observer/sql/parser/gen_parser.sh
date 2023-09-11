#!/bin/bash
flex --outfile /home/xiaoming/miniob/src/observer/sql/parser/lex_sql.cpp --header-file=lex_sql.h /home/xiaoming/miniob/src/observer/sql/parser/lex_sql.l
`which bison` -d --output /home/xiaoming/miniob/src/observer/sql/parser/yacc_sql.cpp /home/xiaoming/miniob/src/observer/sql/parser/yacc_sql.y
