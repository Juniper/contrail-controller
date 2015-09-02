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
    void Update(const std::string &table_name, bool write, bool fail);
    void Get(std::vector<GenDb::DbTableInfo> *vdbti);

 private:
    struct TableStats {
        TableStats() :
            num_reads_(0),
            num_read_fails_(0),
            num_writes_(0),
            num_write_fails_(0) {
        }
        void Update(bool write, bool fail);
        void Get(const std::string &table_name,
            GenDb::DbTableInfo &dbti) const;

        uint64_t num_reads_;
        uint64_t num_read_fails_;
        uint64_t num_writes_;
        uint64_t num_write_fails_;
    };

    friend DbTableStatistics::TableStats operator+(
        const DbTableStatistics::TableStats &a,
        const DbTableStatistics::TableStats &b);
    friend DbTableStatistics::TableStats operator-(
        const DbTableStatistics::TableStats &a,
        const DbTableStatistics::TableStats &b);

    typedef boost::ptr_map<const std::string, TableStats> TableStatsMap;
    TableStatsMap table_stats_map_;
    TableStatsMap otable_stats_map_;
};

}  // namespace GenDb

#endif  // GENDB_GENDB_STATISTICS_H__
