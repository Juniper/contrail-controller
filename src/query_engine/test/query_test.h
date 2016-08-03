/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef QUERY_TEST_H_
#define QUERY_TEST_H_

#include "base/logging.h"
#include <boost/assign/list_of.hpp>
#include <exception>
#include <boost/lexical_cast.hpp>
#include <boost/bind.hpp>
#include <boost/assign/list_of.hpp>
#include "base/task.h"
#include "base/parse_object.h"
#include "io/event_manager.h"
#include "sandesh/sandesh_types.h"
#include "sandesh/sandesh.h"
#include "sandesh/sandesh_session.h"
#include "query.h"
#include "database/cassandra/cql/cql_if.h"

class CdbIfMock : public cass::cql::CqlIf {
public:
    CdbIfMock(EventManager *evm) {
	CqlIf();
	initialize_tables(); };
    ~CdbIfMock() { MessageTable.clear();}

    bool Db_Init();
    bool Db_AddSetTablespace(const std::string& tablespace,const std::string& replication_factor = "1");
    bool Db_GetMultiRow(GenDb::ColListVec *out,
        const std::string &cfname,
        const std::vector<GenDb::DbDataValueVec> &v_rowkey);

private:
    void initialize_tables();
    std::vector<std::map<std::string, std::string> > MessageTable;
};



#endif
 
