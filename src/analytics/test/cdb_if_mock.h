/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "cdb_if.h"

class CdbIfMock : public CdbIf {
public:
    CdbIfMock() :
        CdbIf() {
    }
    ~CdbIfMock() {}

    bool NewDb_AddColumn(std::auto_ptr<GenDb::ColList> cl) {
        return NewDb_AddColumnProxy(cl.get());
    }
    bool AddColumnSync(std::auto_ptr<GenDb::ColList> cl) {
        return AddColumnSyncProxy(cl.get());
    }

    MOCK_METHOD0(Db_Init, bool());
    MOCK_METHOD0(Db_Uninit, void());
    MOCK_METHOD2(Db_AddTablespace, bool(const std::string&,const std::string&));
    MOCK_METHOD1(Db_SetTablespace, bool(const std::string&));
    MOCK_METHOD2(Db_AddSetTablespace, bool(const std::string&,const std::string&));
    MOCK_METHOD1(Db_FindTablespace, bool(const std::string&));

    MOCK_METHOD1(NewDb_AddColumnfamily, bool(const GenDb::NewCf&));
    MOCK_METHOD1(NewDb_AddColumnProxy, bool(GenDb::ColList *cl));
    MOCK_METHOD1(AddColumnSyncProxy, bool(GenDb::ColList *cl));
};
