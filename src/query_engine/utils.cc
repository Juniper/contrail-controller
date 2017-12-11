/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#include <string>
#include <sstream>
#include <base/time_util.h>
#include <base/string_util.h>

#include "json_parse.h"
#include "utils.h"
#include "analytics/viz_types.h"

/*
 * time could be in now +/-10m/h/s format.Parse that and return th
 * UTC corresponding to the parsed val
 * only formats supported are of the format ""\now+1d\"", ""\now-10h\"", etc..
 */
bool parse_time(const std::string& relative_time, uint64_t *usec_time)
{
    uint64_t offset_usec = 0;
    std::string temp;
    if (!relative_time.compare("\"now\"")) {
        *usec_time = UTCTimestampUsec();
        return true;
    } else if (!(relative_time.substr(1,3)).compare("now")) {
        //Find the offset to be shifted
        int found = 0;
        //Extract any number after now
        if ((found = relative_time.find_last_of("h")) > 0) {
            if (!stringToInteger(relative_time.substr(5,relative_time.length()-7), offset_usec)) {
                return false;
            }
            offset_usec = offset_usec*3600*1000000;
        } else if ((found = relative_time.find_last_of("m")) > 0) {
            if (!stringToInteger(relative_time.substr(5,relative_time.length()-7), offset_usec)) {
                return false;
            }
            offset_usec = offset_usec*60*1000000;
        } else if ((found = relative_time.find_last_of("s")) > 0) {
            if (!stringToInteger(relative_time.substr(5,relative_time.length()-7), offset_usec)) {
                return false;
            }
            offset_usec = offset_usec*1000000;
        } else if ((found = relative_time.find_last_of("d")) > 0) {
            if (!stringToInteger(relative_time.substr(5,relative_time.length()-7), offset_usec)) {
                return false;
            }
            offset_usec = offset_usec*24*3600*1000000;
        } else {
            return false;
        }

        //If now+ return UTC + offset else UTC - offset 
        if (!(relative_time.substr(1,4)).compare("now+")) {
            *usec_time = UTCTimestampUsec() + offset_usec;
            return true;
        } else if (!(relative_time.substr(1,4)).compare("now-")) {
            *usec_time = UTCTimestampUsec() - offset_usec;
            return true;
        } else {
            return false;
        }
    } else {
        //To handle old version of input where integer is parsed
        if (!stringToInteger(relative_time, offset_usec)) {
            return false;
        }
        *usec_time = offset_usec;
        return true;
    }
}

GenDb::Op::type get_gendb_op_from_op(int op) {
    switch(op) {
        case EQUAL:
            return GenDb::Op::EQ;
        case LEQ:
            return GenDb::Op::LE;
        case GEQ:
            return GenDb::Op::GE;
        case PREFIX:
            return GenDb::Op::LIKE;
        default:
            return GenDb::Op::INVALID;
    }
}

// Input params are valid.
// No need to add extra validity check.
std::string MsgTableIndexToColumn(const int index) {
    return (g_viz_constants._VIZD_TABLE_SCHEMA.find(
                    g_viz_constants.COLLECTOR_GLOBAL_TABLE))\
                    ->second.columns[index].name;
}

ColIndexType::type MsgTableIndexToIndexType(const int index) {
    return (g_viz_constants._VIZD_TABLE_SCHEMA.find(
                    g_viz_constants.COLLECTOR_GLOBAL_TABLE))\
                    ->second.columns[index].index_type;
}

std::string MsgTableQueryColumnToColumn(const std::string query_column) {
    return (g_viz_constants._VIZD_TABLE_SCHEMA.find(
                    g_viz_constants.COLLECTOR_GLOBAL_TABLE))\
                    ->second.query_column_to_column.find(query_column)->second;
}

std::string MsgTableColumnToQueryColumn(const std::string columnN) {
    return (g_viz_constants._VIZD_TABLE_SCHEMA.find(
                    g_viz_constants.COLLECTOR_GLOBAL_TABLE))\
                    ->second.column_to_query_column.find(columnN)->second;
}
