/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

/*
 * This file has utility functions for handling StatsOracle queries
 * 
 */

#ifndef STATS_QUERY_H_
#define STATS_QUERY_H_

#include <vector>
#include <string>
#include <map>
#include <set>
#include <utility>
#include "query.h"

class AnalyticsQuery;


class StatsQuery {
public:
    static bool is_stat_table_query(const std::string& table);

    struct column_t {
        QEOpServerProxy::VarType datatype;
        bool index;
        bool output;
        std::set<std::string> suffixes;
    };
    std::string type(void) const { return type_; }
    std::string attr(void) const { return attr_; }
    bool is_stat_table_static(void) const { return is_static_; }

    column_t get_column_desc(const std::string& colname) const {
        std::map<std::string,column_t>::const_iterator st = 
            schema_.find(colname);
        if (st!=schema_.end()) {
            return st->second;
        } else {
            column_t ct;
            ct.datatype = QEOpServerProxy::BLANK;
            ct.index = false;
            ct.output = false;
            return ct;
        } 
    }

    StatsQuery(const std::string& table);
private:
    std::string type_;
    std::string attr_;
    bool is_static_;
    std::map<std::string,column_t> schema_;
};


#endif
