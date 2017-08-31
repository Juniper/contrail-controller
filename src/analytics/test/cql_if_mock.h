//
// Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
//

#ifndef ANALYTICS_TEST_CQL_IF_MOCK_H_
#define ANALYTICS_TEST_CQL_IF_MOCK_H_

#include <database/cassandra/cql/cql_if.h>
#include <database/gendb_if.h>

class CqlIfMock : public cass::cql::CqlIf {
 public:
    CqlIfMock() :
        CqlIf() {
    }

    ~CqlIfMock() {}

    bool Db_AddColumn(std::auto_ptr<GenDb::ColList> cl,
        GenDb::DbConsistency::type dconsistency,
        GenDb::GenDbIf::DbAddColumnCb db_cb) {
        return Db_AddColumnProxy(cl.get());
    }

    bool Db_AddColumnSync(std::auto_ptr<GenDb::ColList> cl,
        GenDb::DbConsistency::type dconsistency) {
        return Db_AddColumnSyncProxy(cl.get());
    }

    MOCK_METHOD0(Db_Init, bool());
    MOCK_METHOD0(Db_Uninit, void());

    MOCK_METHOD2(Db_AddTablespace, bool(const std::string&,const std::string&));
    MOCK_METHOD1(Db_SetTablespace, bool(const std::string&));
    MOCK_METHOD2(Db_AddSetTablespace, bool(const std::string&,const std::string&));
    MOCK_METHOD1(Db_FindTablespace, bool(const std::string&));

    MOCK_METHOD1(Db_AddColumnfamily, bool(const GenDb::NewCf&));
    MOCK_METHOD1(Db_AddColumnProxy, bool(GenDb::ColList *cl));
    MOCK_METHOD1(Db_AddColumnSyncProxy, bool(GenDb::ColList *cl));
    MOCK_METHOD5(Db_GetRowAsync, bool(const std::string& cfname,
        const GenDb::DbDataValueVec& rowkey, const GenDb::ColumnNameRange &crange,
        GenDb::DbConsistency::type dconsistency, DbGetRowCb cb));
   MOCK_METHOD6(Db_GetRowAsync, bool(const std::string& cfname,
        const GenDb::DbDataValueVec& rowkey, const GenDb::ColumnNameRange &crange,
        const GenDb::WhereIndexInfoVec& where_vec,
        GenDb::DbConsistency::type dconsistency, DbGetRowCb cb));
};

#endif // ANALYTICS_TEST_CQL_IF_MOCK_H_
