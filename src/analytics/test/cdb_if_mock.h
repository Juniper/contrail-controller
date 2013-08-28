/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "cdb_if.h"

class CdbIfMock : public CdbIf {
public:
    CdbIfMock(boost::asio::io_service *ioservice, GenDb::GenDbIf::DbErrorHandler handler) :
        CdbIf(ioservice, handler, "127.0.0.1", 9160) {
    }

    MOCK_METHOD0(Db_Init, bool());
    MOCK_METHOD0(Db_Uninit, void());
    MOCK_METHOD1(Db_AddTablespace, bool(const std::string&));
    MOCK_METHOD1(Db_SetTablespace, bool(const std::string&));
    MOCK_METHOD1(Db_AddSetTablespace, bool(const std::string&));
    MOCK_METHOD1(Db_FindTablespace, bool(const std::string&));

    MOCK_METHOD2(Db_GetColumnFamilies, void(const std::string, std::vector<std::string>&));
    MOCK_METHOD1(Db_AddColumnfamily, bool(const GenDb::Cf&));
    MOCK_METHOD1(Db_FindColumnfamily, bool(const GenDb::Cf&));
    MOCK_METHOD1(Db_AddColumn, bool(const GenDb::Column&));
    MOCK_METHOD4(Db_GetRangeSlices, bool(std::vector<GenDb::Column>&,const GenDb::Cf&, const GenDb::ColumnRange&, const GenDb::RowKeyRange&));
};
