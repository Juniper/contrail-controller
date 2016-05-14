//
// Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
//

#ifndef GENDB_GENDB_STATISTICS_H__
#define GENDB_GENDB_STATISTICS_H__

#include <boost/ptr_container/ptr_map.hpp>
#include "gendb_types.h"

namespace GenDb {

class DbTableStatistics {
 public:
    DbTableStatistics() {
    }
    void Update(const std::string &table_name, bool write, bool fail,
        bool back_pressure, uint64_t num);
    void GetDiffs(std::vector<GenDb::DbTableInfo> *vdbti);
    void GetCumulative(std::vector<GenDb::DbTableInfo> *vdbti) const;

 private:
    struct TableStats {
        TableStats() :
            num_reads_(0),
            num_read_fails_(0),
            num_writes_(0),
            num_write_fails_(0),
            num_write_back_pressure_fails_(0) {
        }
        void Update(bool write, bool fail, bool back_pressure, uint64_t num);
        void Get(const std::string &table_name,
            GenDb::DbTableInfo *dbti) const;

        uint64_t num_reads_;
        uint64_t num_read_fails_;
        uint64_t num_writes_;
        uint64_t num_write_fails_;
        uint64_t num_write_back_pressure_fails_;
    };

    typedef boost::ptr_map<const std::string, TableStats> TableStatsMap;
    TableStatsMap table_stats_map_;
    // cumulative stats for introspect purpose
    TableStatsMap table_stats_cumulative_map_;
    void GetInternal(std::vector<GenDb::DbTableInfo> *vdbti,
        bool cumulative) const;
};

class IfErrors {
 public:
    IfErrors() :
        write_tablespace_fails_(0),
        read_tablespace_fails_(0),
        write_column_family_fails_(0),
        read_column_family_fails_(0),
        write_column_fails_(0),
        write_batch_column_fails_(0),
        read_column_fails_(0) {
    }
    enum Type {
        ERR_NO_ERROR,
        ERR_WRITE_TABLESPACE,
        ERR_READ_TABLESPACE,
        ERR_WRITE_COLUMN_FAMILY,
        ERR_READ_COLUMN_FAMILY,
        ERR_WRITE_COLUMN,
        ERR_WRITE_BATCH_COLUMN,
        ERR_READ_COLUMN,
    };
    void GetDiffs(GenDb::DbErrors *dbe);
    void GetCumulative(GenDb::DbErrors *dbe) const;
    void Clear();
    void Increment(Type type);

 private:
    uint64_t write_tablespace_fails_;
    uint64_t read_tablespace_fails_;
    uint64_t write_column_family_fails_;
    uint64_t read_column_family_fails_;
    uint64_t write_column_fails_;
    uint64_t write_batch_column_fails_;
    uint64_t read_column_fails_;
    void GetInternal(GenDb::DbErrors *dbe) const;
};

class GenDbIfStats {
 public:
    GenDbIfStats() {
    }
    enum TableOp {
        TABLE_OP_NONE,
        TABLE_OP_WRITE,
        TABLE_OP_WRITE_FAIL,
        TABLE_OP_WRITE_BACK_PRESSURE_FAIL,
        TABLE_OP_READ,
        TABLE_OP_READ_FAIL,
    };
    void IncrementErrors(IfErrors::Type type);
    void IncrementTableStats(TableOp op, const std::string &table_name);
    void IncrementTableWrite(const std::string &table_name);
    void IncrementTableWrite(const std::string &table_name, uint64_t num_writes);
    void IncrementTableWriteFail(const std::string &table_name);
    void IncrementTableWriteBackPressureFail(const std::string &table_name);
    void IncrementTableWriteFail(const std::string &table_name, uint64_t num_writes);
    void IncrementTableRead(const std::string &table_name);
    void IncrementTableRead(const std::string &table_name, uint64_t num_reads);
    void IncrementTableReadFail(const std::string &table_name);
    void IncrementTableReadFail(const std::string &table_name, uint64_t num_reads);
    void GetDiffs(std::vector<GenDb::DbTableInfo> *vdbti, GenDb::DbErrors *dbe);
    void GetCumulative(std::vector<GenDb::DbTableInfo> *vdbti,
        GenDb::DbErrors *dbe) const;

 private:
    void IncrementTableStatsInternal(const std::string &table_name, bool write,
        bool fail, bool back_pressure, uint64_t num);

    GenDb::DbTableStatistics table_stats_;
    IfErrors errors_;
    IfErrors cumulative_errors_;
};

}  // namespace GenDb

#endif  // GENDB_GENDB_STATISTICS_H__
