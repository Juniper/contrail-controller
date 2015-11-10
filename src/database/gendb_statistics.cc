//
// Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
//

#include "gendb_statistics.h"

namespace GenDb {

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
    for (TableStatsMap::const_iterator it = table_stats_map_.begin();
         it != table_stats_map_.end(); it++) {
        const TableStats *table_stats(it->second);
        GenDb::DbTableInfo dbti;
        table_stats->Get(it->first, &dbti);
        vdbti->push_back(dbti);
    }
    table_stats_map_.clear();
}

}  // namespace GenDb
