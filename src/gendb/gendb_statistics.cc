//
// Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
//

#include <analytics/diffstats.h>
#include "gendb_statistics.h"

namespace GenDb {

// TableStats
GenDb::DbTableStatistics::TableStats operator+(
    const GenDb::DbTableStatistics::TableStats &a,
    const GenDb::DbTableStatistics::TableStats &b) {
    GenDb::DbTableStatistics::TableStats sum;
    sum.num_reads_ = a.num_reads_ + b.num_reads_;
    sum.num_read_fails_ = a.num_read_fails_ + b.num_read_fails_;
    sum.num_writes_ = a.num_writes_ + b.num_writes_;
    sum.num_write_fails_ = a.num_write_fails_ + b.num_write_fails_;
    return sum;
}

GenDb::DbTableStatistics::TableStats operator-(
    const GenDb::DbTableStatistics::TableStats &a,
    const GenDb::DbTableStatistics::TableStats &b) {
    GenDb::DbTableStatistics::TableStats diff;
    diff.num_reads_ = a.num_reads_ - b.num_reads_;
    diff.num_read_fails_ = a.num_read_fails_ - b.num_read_fails_;
    diff.num_writes_ = a.num_writes_ - b.num_writes_;
    diff.num_write_fails_ = a.num_write_fails_ - b.num_write_fails_;
    return diff;
}

void GenDb::DbTableStatistics::TableStats::Update(bool write, bool fail) {
    if (write) {
        if (fail) {
            num_write_fails_++;
        } else {
            num_writes_++;
        }
    } else {
        if (fail) {
            num_read_fails_++;
        } else {
            num_reads_++;
        }
    }
}

void GenDb::DbTableStatistics::TableStats::Get(const std::string &table_name,
    DbTableInfo *info) const {
    info->set_table_name(table_name);
    info->set_reads(num_reads_);
    info->set_read_fails(num_read_fails_);
    info->set_writes(num_writes_);
    info->set_write_fails(num_write_fails_);
}

// DbTableStatistics
void GenDb::DbTableStatistics::Update(const std::string &table_name,
    bool write, bool fail) {
    TableStatsMap::iterator it = table_stats_map_.find(table_name);
    if (it == table_stats_map_.end()) {
        it = (table_stats_map_.insert(table_name, new TableStats)).first;
    }
    TableStats *table_stats = it->second;
    table_stats->Update(write, fail);
}

void GenDb::DbTableStatistics::Get(std::vector<GenDb::DbTableInfo> *vdbti) {
    // Send diffs
    GetDiffStats<GenDb::DbTableStatistics::TableStatsMap, const std::string,
        GenDb::DbTableStatistics::TableStats, DbTableInfo>(
        &table_stats_map_, &otable_stats_map_, vdbti);
}

}  // namespace GenDb
