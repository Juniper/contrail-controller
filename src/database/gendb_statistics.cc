//
// Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
//

#include "gendb_statistics.h"

namespace GenDb {

// DbTableStatistics::TableStats
void DbTableStatistics::TableStats::Update(bool write, bool fail,
    bool back_pressure, uint64_t num) {
    if (write) {
        if (fail) {
		num_write_fails_ += num;
        } else if (back_pressure) {
            num_write_back_pressure_fails_ += num;
        } else {
            num_writes_ += num;
        }
    } else {
        if (fail) {
            num_read_fails_ += num;
        } else {
            num_reads_ += num;
        }
    }
}

void DbTableStatistics::TableStats::Get(const std::string &table_name,
    DbTableInfo *info) const {
    info->set_table_name(table_name);
    info->set_reads(num_reads_);
    info->set_read_fails(num_read_fails_);
    info->set_writes(num_writes_);
    info->set_write_fails(num_write_fails_);
    info->set_write_back_pressure_fails(num_write_back_pressure_fails_);
}

// DbTableStatistics
void DbTableStatistics::Update(const std::string &table_name,
    bool write, bool fail, bool back_pressure, uint64_t num) {
    TableStatsMap::iterator it = table_stats_map_.find(table_name);
    if (it == table_stats_map_.end()) {
        it = (table_stats_map_.insert(table_name, new TableStats)).first;
    }
    TableStats *table_stats = it->second;
    table_stats->Update(write, fail, back_pressure, num);
    // Update cumulative table stats as well
    it = table_stats_cumulative_map_.find(table_name);
    if (it == table_stats_cumulative_map_.end()) {
        it = (table_stats_cumulative_map_.insert(table_name, new TableStats)).first;
    }
    table_stats = it->second;
    table_stats->Update(write, fail, back_pressure, num);
}

void DbTableStatistics::GetInternal(std::vector<GenDb::DbTableInfo> *vdbti,
    bool cumulative) const {
    TableStatsMap stats_map;
    if (cumulative) {
        stats_map = table_stats_cumulative_map_ ;
    } else {
        stats_map = table_stats_map_;
    }
    for (TableStatsMap::const_iterator it = stats_map.begin();
         it != stats_map.end(); it++) {
        const TableStats *table_stats(it->second);
        GenDb::DbTableInfo dbti;
        table_stats->Get(it->first, &dbti);
        vdbti->push_back(dbti);
    }
}

void DbTableStatistics::GetDiffs(std::vector<GenDb::DbTableInfo> *vdbti) {
    GetInternal(vdbti, false);
    table_stats_map_.clear();
}

void DbTableStatistics::GetCumulative(std::vector<GenDb::DbTableInfo> *vdbti)
    const {
    GetInternal(vdbti, true);
}

// IfErrors
void IfErrors::GetInternal(DbErrors *db_errors) const {
    db_errors->set_write_tablespace_fails(write_tablespace_fails_);
    db_errors->set_read_tablespace_fails(read_tablespace_fails_);
    db_errors->set_write_table_fails(write_column_family_fails_);
    db_errors->set_read_table_fails(read_column_family_fails_);
    db_errors->set_write_column_fails(write_column_fails_);
    db_errors->set_write_batch_column_fails(write_batch_column_fails_);
    db_errors->set_read_column_fails(read_column_fails_);
}

void IfErrors::Clear() {
    write_tablespace_fails_ = 0;
    read_tablespace_fails_ = 0;
    write_column_family_fails_ = 0;
    read_column_family_fails_ = 0;
    write_column_fails_ = 0;
    write_batch_column_fails_ = 0;
    read_column_fails_ = 0;
}

void IfErrors::GetDiffs(DbErrors *db_errors) {
    GetInternal(db_errors);
    Clear();
}

void IfErrors::GetCumulative(DbErrors *db_errors) const {
    GetInternal(db_errors);
}


void IfErrors::Increment(IfErrors::Type type) {
    switch (type) {
      case IfErrors::ERR_WRITE_TABLESPACE:
        write_tablespace_fails_++;
        break;
      case IfErrors::ERR_READ_TABLESPACE:
        read_tablespace_fails_++;
        break;
      case IfErrors::ERR_WRITE_COLUMN_FAMILY:
        write_column_family_fails_++;
        break;
      case IfErrors::ERR_READ_COLUMN_FAMILY:
        read_column_family_fails_++;
        break;
      case IfErrors::ERR_WRITE_COLUMN:
        write_column_fails_++;
        break;
      case IfErrors::ERR_WRITE_BATCH_COLUMN:
        write_batch_column_fails_++;
        break;
      case IfErrors::ERR_READ_COLUMN:
        read_column_fails_++;
        break;
      default:
        break;
    }
}

// GenDbIfStats
void GenDbIfStats::IncrementTableStatsInternal(const std::string &table_name,
    bool write, bool fail, bool back_pressure, uint64_t num) {
    table_stats_.Update(table_name, write, fail, back_pressure, num);
}

void GenDbIfStats::IncrementTableStats(GenDbIfStats::TableOp op,
    const std::string &table_name) {
    switch (op) {
      case GenDbIfStats::TABLE_OP_NONE:
        break;
      case GenDbIfStats::TABLE_OP_WRITE:
        IncrementTableStatsInternal(table_name, true, false, false, 1);
        break;
      case GenDbIfStats::TABLE_OP_WRITE_FAIL:
        IncrementTableStatsInternal(table_name, true, true, false, 1);
        break;
      case GenDbIfStats::TABLE_OP_WRITE_BACK_PRESSURE_FAIL:
        IncrementTableStatsInternal(table_name, true, true, true, 1);
        break;
      case GenDbIfStats::TABLE_OP_READ:
        IncrementTableStatsInternal(table_name, false, false, false, 1);
        break;
      case GenDbIfStats::TABLE_OP_READ_FAIL:
        IncrementTableStatsInternal(table_name, false, true, false, 1);
        break;
      default:
        break;
    }
}

void GenDbIfStats::IncrementTableWrite(const std::string &table_name) {
    IncrementTableStatsInternal(table_name, true, false, false, 1);
}

void GenDbIfStats::IncrementTableWrite(const std::string &table_name,
    uint64_t num_writes) {
    IncrementTableStatsInternal(table_name, true, false, false, num_writes);
}

void GenDbIfStats::IncrementTableWriteFail(const std::string &table_name) {
    IncrementTableStatsInternal(table_name, true, true, false, 1);
}

void GenDbIfStats::IncrementTableWriteFail(const std::string &table_name,
    uint64_t num_writes) {
    IncrementTableStatsInternal(table_name, true, true, false, num_writes);
}

void GenDbIfStats::IncrementTableWriteBackPressureFail(
    const std::string &table_name) {
    IncrementTableStatsInternal(table_name, true, false, true, 1);
}

void GenDbIfStats::IncrementTableRead(const std::string &table_name) {
    IncrementTableStatsInternal(table_name, false, false, false, 1);
}

void GenDbIfStats::IncrementTableRead(const std::string &table_name,
    uint64_t num_reads) {
    IncrementTableStatsInternal(table_name, false, false, false, num_reads);
}

void GenDbIfStats::IncrementTableReadFail(const std::string &table_name) {
    IncrementTableStatsInternal(table_name, false, true, false, 1);
}

void GenDbIfStats::IncrementTableReadFail(const std::string &table_name,
    uint64_t num_reads) {
    IncrementTableStatsInternal(table_name, false, true, false, num_reads);
}

void GenDbIfStats::IncrementErrors(IfErrors::Type etype) {
    errors_.Increment(etype);
    cumulative_errors_.Increment(etype);
}

void GenDbIfStats::GetDiffs(std::vector<DbTableInfo> *vdbti, DbErrors *dbe) {
    // Get diff cfstats
    table_stats_.GetDiffs(vdbti);
    // Get diff errors
    errors_.GetDiffs(dbe);
}

void GenDbIfStats::GetCumulative(std::vector<DbTableInfo> *vdbti,
     DbErrors *dbe) const {
    // Get cumulative cfstats
    table_stats_.GetCumulative(vdbti);
    // Get cumulative errors
    cumulative_errors_.GetCumulative(dbe);
}

}  // namespace GenDb
