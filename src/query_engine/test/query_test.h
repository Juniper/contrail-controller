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
#include "cdb_if.h"

class CdbIfMock : public CdbIf {
public:
    CdbIfMock(boost::asio::io_service *ioservice, 
            GenDb::GenDbIf::DbErrorHandler handler) :
        CdbIf(ioservice, handler, "127.0.0.1", 9160) { initialize_tables(); };
    ~CdbIfMock() { MessageTable.clear();}

    bool Db_Init();
    bool Db_AddSetTablespace(const std::string& tablespace,const std::string& replication_factor = "1");
    bool Db_GetRangeSlices(std::vector<GenDb::Column>&,const GenDb::Cf&, const GenDb::ColumnRange&, const GenDb::RowKeyRange&);
    bool Db_GetMultiRow(std::map<std::string, std::vector<GenDb::ColElement> >& ret,
                const std::string& cfname, const std::vector<std::string>& key);

private:
    void initialize_tables();
    std::vector<std::map<std::string, std::string> > MessageTable;
    // return data for a particular MessageTable index
    bool Db_GetStringIndexRange(std::string index_field, std::vector<GenDb::Column>&,const GenDb::Cf&, const GenDb::ColumnRange&, const GenDb::RowKeyRange&);
};



#endif
 
