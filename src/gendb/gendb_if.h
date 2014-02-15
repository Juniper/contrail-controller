/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __GENDB_IF_H__
#define __GENDB_IF_H__

#include <stdint.h>
#include <string>
#include <vector>
#include <map>
#include <boost/function.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/variant.hpp>

#include "gendb_types.h"

namespace GenDb {

/* New stuff */
typedef boost::variant<std::string, uint64_t, uint32_t, boost::uuids::uuid, uint8_t, uint16_t, double> DbDataValue;
enum DbDataValueType {
    DB_VALUE_STRING = 0,
    DB_VALUE_UINT64 = 1,
    DB_VALUE_UINT32 = 2,
    DB_VALUE_UUID = 3,
    DB_VALUE_UINT8 = 4,
    DB_VALUE_UINT16 = 5,
    DB_VALUE_DOUBLE = 6,
};    
typedef std::vector<DbDataValue> DbDataValueVec;
typedef std::vector<GenDb::DbDataType::type> DbDataTypeVec;

struct NewCf {
    enum ColumnFamilyType {
        COLUMN_FAMILY_INVALID = 0,
        COLUMN_FAMILY_SQL = 1,
        COLUMN_FAMILY_NOSQL = 2,
    };
    typedef std::map<std::string, GenDb::DbDataType::type> SqlColumnMap; /* columns meta-data */

    NewCf(const std::string& cfname, const DbDataTypeVec& key_type,
            const SqlColumnMap& cfcolumns) :
        cfname_(cfname),
        cftype_(COLUMN_FAMILY_SQL),
        key_validation_class(key_type),
        cfcolumns_(cfcolumns) {
    }

    NewCf(const std::string& cfname, const DbDataTypeVec& key_type,
            const DbDataTypeVec& comp_type,
            const DbDataTypeVec& valid_class) :
        cfname_(cfname),
        cftype_(COLUMN_FAMILY_NOSQL),
        key_validation_class(key_type),
        comparator_type(comp_type),
        default_validation_class(valid_class) {
    }

    ~NewCf() {}

    std::string cfname_;
    ColumnFamilyType cftype_;
    DbDataTypeVec key_validation_class; /* for key-value comparison */
    SqlColumnMap cfcolumns_; /* columns meta-data */
    DbDataTypeVec comparator_type; /* for column-name comparison */
    DbDataTypeVec default_validation_class; /* for column-value comparison */
};

struct NewCol {
    NewCol(const DbDataValueVec& n, const DbDataValueVec& v, int ttl=-1) :
        cftype_(NewCf::COLUMN_FAMILY_NOSQL), name(n), value(v), ttl(ttl) {}

    NewCol(const std::string& n, const DbDataValue& v, int ttl=-1);

    NewCf::ColumnFamilyType cftype_;
    DbDataValueVec name;
    DbDataValueVec value;
    int ttl;
};

struct ColList {
    ColList() {
    }

    ~ColList() {
    }

    std::string cfname_; /* column family name */
    DbDataValueVec rowkey_; /* rowkey-value */
    std::vector<NewCol> columns_; // only one of these is expected to be filled
};

struct ColumnNameRange {
    ColumnNameRange() : count(100) {
    }

    ~ColumnNameRange() {
    }

    DbDataValueVec start_;
    DbDataValueVec finish_;

    uint32_t count;
};

class GenDbIf {
    public:
        typedef boost::function<void(void)> DbErrorHandler;
        typedef boost::function<void(size_t)> DbQueueWaterMarkCb;

        GenDbIf() {}
        virtual ~GenDbIf() {}

        /* Init function */
        virtual bool Db_Init(std::string task_id, int task_instance) = 0;
        virtual void Db_Uninit(std::string task_id, int task_instance) = 0;
        virtual void Db_SetInitDone(bool init_done) = 0;
        /* api to create a table space */
        virtual bool Db_AddTablespace(const std::string& tablespace,const std::string& replication_factor) = 0;
        /* api to set the current table space */
        virtual bool Db_SetTablespace(const std::string& tablespace) = 0;
        /* api to set the current table space */
        virtual bool Db_AddSetTablespace(const std::string& tablespace,const std::string& replication_factor="1") = 0;
        /* api to add a column family in the current table space */
        virtual bool Db_FindTablespace(const std::string& tablespace) = 0;

        /* api to add a column family in the current table space */
        virtual bool NewDb_AddColumnfamily(const NewCf& cf) = 0;

        virtual bool Db_UseColumnfamily(const NewCf& cf) = 0;

        /* api to add a column in the current table space */
        virtual bool NewDb_AddColumn(std::auto_ptr<ColList> cl) = 0;
        virtual bool AddColumnSync(std::auto_ptr<GenDb::ColList> cl) = 0;

        virtual bool Db_GetRow(ColList& ret, const std::string& cfname,
                const DbDataValueVec& rowkey) = 0;
        virtual bool Db_GetMultiRow(std::vector<ColList>& ret,
                const std::string& cfname, const std::vector<DbDataValueVec>& key,
                GenDb::ColumnNameRange *crange_ptr = NULL) = 0;
        /* api to get range of column data for a range of rows */
        virtual bool Db_GetRangeSlices(ColList& col_list,
                const std::string& cfname, const ColumnNameRange& crange,
                const DbDataValueVec& key) = 0;
        virtual bool Db_GetQueueStats(uint64_t &queue_count, uint64_t &enqueues) const = 0;
        virtual void Db_SetQueueWaterMark(bool high, size_t queue_count, DbQueueWaterMarkCb cb) = 0;
        virtual void Db_ResetQueueWaterMarks() = 0;

        static GenDbIf *GenDbIfImpl(boost::asio::io_service *ioservice, DbErrorHandler hdlr, 
                std::string cassandra_ip, unsigned short cassandra_port, 
                int analytics_ttl, std::string name);
};

} // namespace GenDb

#endif
