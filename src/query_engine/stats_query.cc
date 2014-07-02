/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

/*
 * This file has utility functions for handling StatsOracle queries
 *
 */

#include "stats_query.h"

using std::string;

bool
StatsQuery::is_stat_table_query(const std::string & tname) {
    if (tname.compare(0, g_viz_constants.STAT_VT_PREFIX.length(),
            g_viz_constants.STAT_VT_PREFIX)) {
        return false;
    }
    return true;
}

StatsQuery::StatsQuery(const std::string & tname) {

    QE_ASSERT(is_stat_table_query(tname));
    size_t tpos,apos;
    tpos = tname.find('.');
    apos = tname.find('.', tpos+1);
    type_ = tname.substr(tpos+1, apos-tpos-1);
    attr_ = tname.substr(apos+1, std::string::npos);        

    std::map<std::string,column_t>& s = schema_;

    is_static_ = false;
    for (uint idx=0;
         idx<g_viz_constants._STAT_TABLES.size() + g_viz_constants._STAT_TEST_TABLES.size();
         idx++) {
        const stat_table& st = idx < g_viz_constants._STAT_TABLES.size() ? 
             g_viz_constants._STAT_TABLES[idx] :
             g_viz_constants._STAT_TEST_TABLES[idx-g_viz_constants._STAT_TABLES.size()];
        if ((st.stat_type != type_)||
            (st.stat_attr != attr_)) {
            continue;
        }
        is_static_ = true;
        for (uint j=0; j<st.attributes.size(); j++) {
            column_t c;
            std::string dtype(st.attributes[j].datatype);
            if (dtype=="string") {
                c.datatype = QEOpServerProxy::STRING;
            } else if (dtype=="double") {
                c.datatype = QEOpServerProxy::DOUBLE;
            } else {
                c.datatype = QEOpServerProxy::UINT64;
            }
            c.index = st.attributes[j].index;
            c.output = true;
            std::set<std::string> sf(
                st.attributes[j].suffixes.begin(),
                st.attributes[j].suffixes.end());
            c.suffixes = sf;

            s[st.attributes[j].name] = c;
        }
    }
    if (s.find(g_viz_constants.STAT_OBJECTID_FIELD)==s.end()) {
        column_t c;
        c.datatype = QEOpServerProxy::STRING;
        c.index = true;
        c.output = true;
        s[g_viz_constants.STAT_OBJECTID_FIELD] = c;
    }
    {
        column_t c;
        c.datatype = QEOpServerProxy::STRING;
        c.index = true;
        c.output = false;
        s[g_viz_constants.STAT_SOURCE_FIELD] = c;
    }
    {
        column_t c;
        c.datatype = QEOpServerProxy::UUID;
        c.index = false;
        c.output = true;
        s[g_viz_constants.STAT_UUID_FIELD] = c;
    }
}

