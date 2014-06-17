/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "query.h"

#include "boost/uuid/uuid_io.hpp"
#include "base/util.h"
#include "rapidjson/document.h"

#include "analytics/vizd_table_desc.h"
#include "stats_select.h"

using std::string;

SelectQuery::SelectQuery(QueryUnit *main_query,
        std::map<std::string, std::string> json_api_data):
    QueryUnit(main_query, main_query),
    provide_timeseries(false),
    granularity(0),
    fs_query_type_(SelectQuery::FS_SELECT_INVALID) {

    AnalyticsQuery *m_query = (AnalyticsQuery *)main_query;
    result_.reset(new BufT);
    mresult_.reset(new MapBufT);

    // initialize Cassandra related fields
    if (
            (m_query->table() == g_viz_constants.COLLECTOR_GLOBAL_TABLE) ||
            (m_query->is_object_table_query())
       )
    {
        cfname = g_viz_constants.COLLECTOR_GLOBAL_TABLE;
    } else if ((m_query->table() == (g_viz_constants.FLOW_TABLE)) ||
            (m_query->table() == (g_viz_constants.OBJECT_VALUE_TABLE))) {
        cfname = m_query->table();
    } else {
        // this is a flow series query or stats query
    }

    QE_TRACE(DEBUG,  "cfname :" << cfname);

    // Do JSON parsing of main SELECT fields
    std::map<std::string, std::string>::iterator iter;
    iter = json_api_data.find(QUERY_SELECT);
    QE_PARSE_ERROR(iter != json_api_data.end());

    rapidjson::Document d;
    std::string json_string = "{ \"select\" : " + 
        iter->second + " }";
    json_string_ = json_string;
    QE_TRACE(DEBUG, "parsing through rapidjson: " << json_string);
    d.Parse<0>(const_cast<char *>(json_string.c_str()));
    const rapidjson::Value& json_select_fields = d["select"]; 
    QE_PARSE_ERROR(json_select_fields.IsArray());
    QE_TRACE(DEBUG, "# of select fields is " << json_select_fields.Size());

    if (m_query->is_stat_table_query()) {
        for (rapidjson::SizeType i = 0; i < json_select_fields.Size(); i++) {
            select_column_fields.push_back(json_select_fields[i].GetString());
        }
        stats_.reset(new StatsSelect(m_query, select_column_fields));
        QE_INVALIDARG_ERROR(stats_->Status());
        return;
    }     
    // whether uuid key was provided in select field
    bool uuid_key_selected = false;

    for (rapidjson::SizeType i = 0; i < json_select_fields.Size(); i++) 
    {
        QE_PARSE_ERROR(json_select_fields[i].IsString());
        QE_TRACE(DEBUG, "Select field string: " << 
                json_select_fields[i].GetString());

        // processing "T" or "T=" field
        if (json_select_fields[i].GetString() == 
                std::string(TIMESTAMP_FIELD))
        {
            provide_timeseries = true;
            select_column_fields.push_back(TIMESTAMP_FIELD);
            QE_INVALIDARG_ERROR(m_query->is_flow_query() ||
                m_query->is_stat_table_query());
        } else if (boost::starts_with(json_select_fields[i].GetString(),
                    TIMESTAMP_GRANULARITY))
        {
            provide_timeseries = true;
            std::string timestamp_str = json_select_fields[i].GetString();
            granularity = atoi(timestamp_str.c_str() + 
                + sizeof(TIMESTAMP_GRANULARITY)-1);
            granularity = granularity * kMicrosecInSec;
            select_column_fields.push_back(TIMESTAMP_GRANULARITY);
            QE_INVALIDARG_ERROR(
                m_query->table() == g_viz_constants.FLOW_SERIES_TABLE ||
                m_query->is_stat_table_query());
        } 
        else if (json_select_fields[i].GetString() ==
                std::string(SELECT_PACKETS)) {
            agg_stats_t agg_stats_entry = {RAW, PKT_STATS};
            agg_stats.push_back(agg_stats_entry);
            QE_INVALIDARG_ERROR(m_query->is_flow_query());
        }
        else if (json_select_fields[i].GetString() ==
                std::string(SELECT_BYTES)) {
            agg_stats_t agg_stats_entry = {RAW, BYTE_STATS};
            agg_stats.push_back(agg_stats_entry);
            QE_INVALIDARG_ERROR(m_query->is_flow_query());
        }
        else if (json_select_fields[i].GetString() ==
                std::string(SELECT_SUM_PACKETS)) {
            agg_stats_t agg_stats_entry = {SUM, PKT_STATS};
            agg_stats.push_back(agg_stats_entry);
            QE_INVALIDARG_ERROR(
                m_query->table() == g_viz_constants.FLOW_SERIES_TABLE);
        }
        else if (json_select_fields[i].GetString() ==
                std::string(SELECT_SUM_BYTES)) {
            agg_stats_t agg_stats_entry = {SUM, BYTE_STATS};
            agg_stats.push_back(agg_stats_entry);
            QE_INVALIDARG_ERROR(
                m_query->table() == g_viz_constants.FLOW_SERIES_TABLE);
        }      
        // processing other select fields
        else {
            QE_INVALIDARG_ERROR(is_valid_select_field(
                        json_select_fields[i].GetString()));
            select_column_fields.push_back(
                get_column_name(json_select_fields[i].GetString()));
            if (json_select_fields[i].GetString() == g_viz_constants.UUID_KEY)
                uuid_key_selected = true;
        }
    }

    if (m_query->table() == g_viz_constants.FLOW_SERIES_TABLE) {
        evaluate_fs_query_type();
    }

    if ((m_query->table() == g_viz_constants.FLOW_TABLE) && !uuid_key_selected) {
        select_column_fields.push_back(g_viz_constants.UUID_KEY);
    }
}

bool SelectQuery::is_valid_select_field(const std::string& select_field) const {
    AnalyticsQuery *mquery = (AnalyticsQuery*)main_query;
    const std::string& table = mquery->table();

    for(size_t i = 0; i < g_viz_constants._TABLES.size(); i++) {
        if (g_viz_constants._TABLES[i].name == table) {
            for (size_t j = 0; 
                j < g_viz_constants._TABLES[i].schema.columns.size(); j++) {
                if (g_viz_constants._TABLES[i].schema.columns[j].name ==
                        select_field)
                    return true;
            }
            return false;
        }
    }

    for (std::map<std::string, objtable_info>::const_iterator it =
            g_viz_constants._OBJECT_TABLES.begin();
            it != g_viz_constants._OBJECT_TABLES.end(); it++) {
        if (it->first == table) {
            for (size_t j = 0; 
                j < g_viz_constants._OBJECT_TABLE_SCHEMA.columns.size(); j++) {
                if (g_viz_constants._OBJECT_TABLE_SCHEMA.columns[j].name ==
                        select_field)
                    return true;
            }
            return false;
        }
    }
    return false;
}

bool SelectQuery::is_flow_tuple_specified() {
    for (std::vector<std::string>::const_iterator it = 
         select_column_fields.begin(); it != select_column_fields.end(); ++it) {
        std::string qstring(get_query_string(*it));
        if (qstring == g_viz_constants.FlowRecordNames[
                        FlowRecordFields::FLOWREC_VROUTER] ||
            qstring == g_viz_constants.FlowRecordNames[
                        FlowRecordFields::FLOWREC_SOURCEVN] ||
            qstring == g_viz_constants.FlowRecordNames[
                        FlowRecordFields::FLOWREC_SOURCEIP] ||
            qstring == g_viz_constants.FlowRecordNames[
                        FlowRecordFields::FLOWREC_DESTVN] ||
            qstring == g_viz_constants.FlowRecordNames[
                        FlowRecordFields::FLOWREC_DESTIP] ||
            qstring == g_viz_constants.FlowRecordNames[
                        FlowRecordFields::FLOWREC_PROTOCOL] ||
            qstring == g_viz_constants.FlowRecordNames[
                        FlowRecordFields::FLOWREC_SPORT] ||
            qstring == g_viz_constants.FlowRecordNames[
                        FlowRecordFields::FLOWREC_DPORT] ||
            qstring == g_viz_constants.FlowRecordNames[
                        FlowRecordFields::FLOWREC_DIRECTION_ING]) {
            return true;
        }
    }
    return false;
}

void SelectQuery::evaluate_fs_query_type() {
    if (provide_timeseries) {
        if (granularity) {
            fs_query_type_ |= SelectQuery::FS_SELECT_TS;
        } else {
            fs_query_type_ |= SelectQuery::FS_SELECT_T; 
        }
    }
    if (is_flow_tuple_specified()) {
        fs_query_type_ |= SelectQuery::FS_SELECT_FLOW_TUPLE;
    }
    if (agg_stats.size()) {
        fs_query_type_ |= SelectQuery::FS_SELECT_STATS;
        if (!provide_timeseries) {
            std::vector<agg_stats_t>::const_iterator it;
            for (it = agg_stats.begin(); it != agg_stats.end(); ++it) {
                if ((*it).agg_op == RAW) {
                    fs_query_type_ |= SelectQuery::FS_SELECT_T;
                    break;
                }
            }
        }
    }
}

query_status_t SelectQuery::process_query() {

    if (status_details != 0)
    {
        QE_TRACE(DEBUG, 
             "No need to process query, as there were errors previously");
        return QUERY_FAILURE;
    }


    /*
     * various select queries
     *  flow related queries
     *    select T=5, sum(pkts)...
     *    select <x-tuple>, sum(pkts)...
     *    select T=5, <x-tuple>, sum(pkts)...
     *
     *    select T, pkts...
     *    select <x-tuple>, pkts...
     *    select T, <x-tuple>, pkts...
     *
     *    For all the above queries the output from select will be
     *    a series of rows of
     *     T, <x-tuple>, pkts...
     *    it's expected that aggregation and binning will be done by
     *    the next level
     *
     *  message related queries
     */
    AnalyticsQuery *m_query = (AnalyticsQuery *)main_query;
    std::vector<query_result_unit_t>& query_result =
        m_query->wherequery_->query_result;
    boost::shared_ptr<QueryResultMetaData> nullmetadata;

    if (m_query->table() == g_viz_constants.FLOW_SERIES_TABLE) {
        QE_TRACE(DEBUG, "Flow Series query type: " << fs_query_type_);
        process_fs_query_cb_map_t::const_iterator query_cb_it = 
            process_fs_query_cb_map_.find(fs_query_type_);
        QE_ASSERT(query_cb_it != process_fs_query_cb_map_.end());
        populate_fs_result_cb_map_t::const_iterator result_cb_it = 
            populate_fs_result_cb_map_.find(fs_query_type_);
        QE_ASSERT(result_cb_it != populate_fs_result_cb_map_.end());
        process_fs_query(query_cb_it->second, result_cb_it->second);
    } else if (m_query->table() == (g_viz_constants.FLOW_TABLE)) {

        std::vector<GenDb::DbDataValueVec> keys;
        std::set<boost::uuids::uuid> uuid_list;
        // can not handle query result of huge size
        if (query_result.size() > (size_t)query_result_size_limit)
        {
            QE_LOG(DEBUG, 
            "Can not handle query result of size:" << query_result.size());
            QE_IO_ERROR_RETURN(0, QUERY_FAILURE);
        }

        for (std::vector<query_result_unit_t>::iterator it = query_result.begin();
                it != query_result.end(); it++) {
            boost::uuids::uuid u;
            flow_stats stats;

            it->get_uuid_stats(u, stats);
            GenDb::DbDataValueVec a_key;
            a_key.push_back(u);
            keys.push_back(a_key);

            uuid_list.insert(u);
        }

        GenDb::ColListVec mget_res;
        if (!m_query->dbif->Db_GetMultiRow(mget_res, g_viz_constants.FLOW_TABLE, keys)) {
            QE_IO_ERROR_RETURN(0, QUERY_FAILURE);
        }

        std::vector<GenDb::NewCf>::const_iterator fit;
        for (fit = vizd_flow_tables.begin(); fit != vizd_flow_tables.end(); fit++) {
            if (fit->cfname_ == g_viz_constants.FLOW_TABLE)
                break;
        }
        if (fit == vizd_flow_tables.end())
            VIZD_ASSERT(0);
        const GenDb::NewCf::SqlColumnMap& sql_cols = fit->cfcolumns_;
        GenDb::NewCf::SqlColumnMap::const_iterator col_type_it;

        for (GenDb::ColListVec::iterator it = mget_res.begin();
                it != mget_res.end(); it++) {
            boost::uuids::uuid u;
            assert(it->rowkey_.size() > 0);
            try {
                u = boost::get<boost::uuids::uuid>(it->rowkey_.at(0));
            } catch (boost::bad_get& ex) {
                QE_ASSERT(0);
            }
            std::map<std::string, GenDb::DbDataValue> col_res_map;

            // extract uuid
            std::set<boost::uuids::uuid>::const_iterator uuid_list_it;
            uuid_list_it = uuid_list.find(u);
            if (uuid_list_it == uuid_list.end())
            {
                QE_IO_ERROR_RETURN(0, QUERY_FAILURE);
            }

            for (GenDb::NewColVec::iterator jt = it->columns_.begin();
                    jt != it->columns_.end(); jt++) {
                QE_ASSERT(jt->name->size() == 1 &&
                        jt->value->size() == 1);
                std::string col_name;
                try {
                    col_name = boost::get<std::string>(jt->name->at(0));
                } catch (boost::bad_get& ex) {
                    QE_ASSERT(0);
                }

                col_res_map.insert(std::make_pair(col_name, jt->value->at(0)));
            }
            if (!col_res_map.size()) {
                QE_LOG(ERROR, "No entry for uuid: " << UuidToString(u) <<
                       " in Analytics db");
                continue;
            }

            std::map<std::string, std::string> cmap;
            for (std::vector<std::string>::iterator jt = select_column_fields.begin();
                    jt != select_column_fields.end(); jt++) {

                if (*jt == "agg-packets") {
                    std::string pkts(g_viz_constants.FlowRecordNames[
                                    FlowRecordFields::FLOWREC_PACKETS]);
                    std::map<std::string, GenDb::DbDataValue>::const_iterator pt = 
                        col_res_map.find(pkts);
                    QE_ASSERT(pt != col_res_map.end());
                    cmap.insert(std::make_pair("agg-packets", 
                                integerToString(pt->second)));
                    continue;
                }
                if (*jt == "agg-bytes") {
                    std::string bytes(g_viz_constants.FlowRecordNames[
                                    FlowRecordFields::FLOWREC_BYTES]);
                    std::map<std::string, GenDb::DbDataValue>::const_iterator bt = 
                        col_res_map.find(bytes);
                    QE_ASSERT(bt != col_res_map.end());
                    cmap.insert(std::make_pair("agg-bytes", 
                                integerToString(bt->second)));
                    continue;
                }
                if (*jt == "UuidKey") {
                    std::string u_ss = boost::lexical_cast<std::string>(u);

                    cmap.insert(std::make_pair("UuidKey", u_ss));
                    continue;
                }

                std::map<std::string, GenDb::DbDataValue>::iterator kt = col_res_map.find(*jt);
                if (kt == col_res_map.end()) {
                    // rather than asserting just return empty string
                    cmap.insert(std::make_pair(*jt, std::string("0")));
                    continue;
                }

                if ((col_type_it = sql_cols.find(kt->first)) == sql_cols.end()) {
                    // valid assert, this is a bug
                    QE_ASSERT(0);
                }

                std::string elem_value;
                switch (col_type_it->second) {
                    case GenDb::DbDataType::Unsigned8Type:
                          {
                            uint8_t val;
                            try {
                                val = boost::get<uint8_t>(kt->second);
                            } catch (boost::bad_get& ex) {
                                QE_ASSERT(0);
                            }
                            elem_value = integerToString(val);

                            break;
                          }
                    case GenDb::DbDataType::Unsigned16Type:
                          {
                            uint16_t val;
                            try {
                                val = boost::get<uint16_t>(kt->second);
                            } catch (boost::bad_get& ex) {
                                QE_ASSERT(0);
                            }
                            elem_value = integerToString(val);

                            break;
                          }
                    case GenDb::DbDataType::Unsigned32Type:
                          {
                            uint32_t val;
                            try {
                                val = boost::get<uint32_t>(kt->second);
                            } catch (boost::bad_get& ex) {
                                QE_ASSERT(0);
                            }
                            elem_value = integerToString(val);

                            break;
                          }
                    case GenDb::DbDataType::Unsigned64Type:
                          {
                            uint64_t val;
                            try {
                                val = boost::get<uint64_t>(kt->second);
                            } catch (boost::bad_get& ex) {
                                QE_ASSERT(0);
                            }
                            elem_value = integerToString(val);

                            break;
                          }
                    case GenDb::DbDataType::AsciiType:
                          {
                            try {
                                elem_value = boost::get<std::string>(kt->second);
                            } catch (boost::bad_get& ex) {
                                QE_ASSERT(0);
                            }

                            break;
                          }
                    default:
                        QE_ASSERT(0);
                }

                cmap.insert(std::make_pair(kt->first, elem_value));
            }
            result_->push_back(std::make_pair(cmap, nullmetadata));
        }
    } else if (m_query->is_stat_table_query()) {
        QE_ASSERT(stats_.get());
        // can not handle query result of huge size
        if (query_result.size() > (size_t)query_result_size_limit)
        {
            QE_LOG(DEBUG, 
            "Can not handle query result of size:" << query_result.size());
            QE_IO_ERROR_RETURN(0, QUERY_FAILURE);
        }

        //uint64_t parset=0;
        //uint64_t loadt=0;
        //uint64_t jsont=0;
        for (std::vector<query_result_unit_t>::iterator it = query_result.begin();
                it != query_result.end(); it++) {

            string json_string;
            GenDb::DbDataValue value;
            boost::uuids::uuid u;

            it->get_stattable_info(json_string, u);

            //uint64_t thenj = UTCTimestampUsec();
            rapidjson::Document d;
            d.Parse<0>(const_cast<char *>(json_string.c_str()));
            //jsont += UTCTimestampUsec() - thenj;

            std::vector<StatsSelect::StatEntry> attribs;
            {
                for (rapidjson::Value::ConstMemberIterator itr = d.MemberBegin();
                        itr != d.MemberEnd(); ++itr) {
                    QE_ASSERT(itr->name.IsString());

                    //uint64_t thenp = UTCTimestampUsec();
                    std::string fvname(itr->name.GetString());
                    char tname = fvname[fvname.length()-1];

                    StatsSelect::StatEntry se;
                    se.name = fvname.substr(0,fvname.length()-2);
                    if (tname == 's') {
                        se.value = itr->value.GetString();
                    } else if (tname == 'n') {
                        se.value = (uint64_t)itr->value.GetUint();
                    } else if (tname == 'd') {
                        if (itr->value.IsDouble())
                            se.value = (double) itr->value.GetDouble();
                        else
                            se.value = (double) itr->value.GetUint();
                    } else {
                        QE_ASSERT(0);
                    }
                    attribs.push_back(se);

                    //parset += UTCTimestampUsec() - thenp; 
                }
            }
            //uint64_t thenl = UTCTimestampUsec();
            stats_->LoadRow(u, it->timestamp, attribs, *mresult_);
            //loadt += UTCTimestampUsec() - thenl; 
        }
        //QE_TRACE(DEBUG, "Select ProcTime - Entries : " << query_result.size() <<
        //        " json : " << jsont << " parse : " << parset << " load : " << loadt);

    } else if (m_query->table() == (g_viz_constants.OBJECT_VALUE_TABLE)) {
        uint32_t t2_start = m_query->from_time() >> g_viz_constants.RowTimeInBits;
        uint32_t t2_end = m_query->end_time() >> g_viz_constants.RowTimeInBits;
        uint32_t t1_start = m_query->from_time() & g_viz_constants.RowTimeInMask;
        uint32_t t1_end = m_query->end_time() & g_viz_constants.RowTimeInMask;

        std::vector<GenDb::DbDataValueVec> keys;
        for (uint32_t t2 = t2_start; t2 < t2_end; t2++) {
            GenDb::DbDataValueVec a_key;
            a_key.push_back(t2);
            a_key.push_back(m_query->object_value_key);
            keys.push_back(a_key);
        }

        GenDb::ColListVec mget_res;
        if (!m_query->dbif->Db_GetMultiRow(mget_res, g_viz_constants.OBJECT_VALUE_TABLE, keys)) {
            QE_IO_ERROR_RETURN(0, QUERY_FAILURE);
        }

        std::set<std::string> unique_values;

        GenDb::ColListVec::iterator first_it = mget_res.begin();
        GenDb::ColListVec::iterator last_it = mget_res.begin();
        if (mget_res.size() > 0)
            std::advance(last_it, mget_res.size()-1);
        for (GenDb::ColListVec::iterator it = mget_res.begin();
                it != mget_res.end(); it++) {
            for (GenDb::NewColVec::iterator jt = it->columns_.begin();
                    jt != it->columns_.end(); jt++) {
                if (it == first_it) {
                    uint32_t t1;
                    try {
                        t1 = boost::get<uint32_t>(jt->name->at(0));
                    } catch (boost::bad_get& ex) {
                        assert(0);
                    }
                    if (t1 < t1_start)
                        continue;
                }
                if (it == last_it) {
                    uint32_t t1;
                    try {
                        t1 = boost::get<uint32_t>(jt->name->at(0));
                    } catch (boost::bad_get& ex) {
                        assert(0);
                    }
                    if (t1 > t1_end)
                        break;
                }
                std::string value;
                try {
                    value = boost::get<std::string>(jt->value->at(0));
                } catch (boost::bad_get& ex) {
                    assert(0);
                }
                unique_values.insert(value);
            }
        }
        
        for (std::set<std::string>::iterator it = unique_values.begin();
                it != unique_values.end(); it++) {
            std::map<std::string, std::string> cmap;
            cmap.insert(std::make_pair(g_viz_constants.OBJECT_ID, *it));
            result_->push_back(std::make_pair(cmap, nullmetadata));
        }

    } else {
        std::vector<GenDb::DbDataValueVec> keys;
        // can not handle query result of huge size
        if (query_result.size() > (size_t)query_result_size_limit)
        {
            QE_LOG(DEBUG, 
            "Can not handle query result of size:" << query_result.size());
            QE_IO_ERROR_RETURN(0, QUERY_FAILURE);
        }

        for (std::vector<query_result_unit_t>::iterator it = query_result.begin();
                it != query_result.end(); it++) {
            boost::uuids::uuid u;

            it->get_uuid(u);

            GenDb::DbDataValueVec a_key;
            a_key.push_back(u);
            keys.push_back(a_key);
        }

        GenDb::ColListVec mget_res;
        if (!m_query->dbif->Db_GetMultiRow(mget_res, 
                    g_viz_constants.COLLECTOR_GLOBAL_TABLE, keys)) {
            QE_IO_ERROR_RETURN(0, QUERY_FAILURE);
        }
        for (GenDb::ColListVec::iterator it = mget_res.begin();
                it != mget_res.end(); it++) {
            std::map<std::string, GenDb::DbDataValue> col_res_map;
            for (GenDb::NewColVec::iterator jt = it->columns_.begin();
                    jt != it->columns_.end(); jt++) {
                std::string col_name;
                try {
                    col_name = boost::get<std::string>(jt->name->at(0));
                } catch (boost::bad_get& ex) {
                    QE_ASSERT(0);
                }

                col_res_map.insert(std::make_pair(col_name, jt->value->at(0)));
            }
            if (!col_res_map.size()) {
                boost::uuids::uuid u;
                assert(it->rowkey_.size() > 0);
                try {
                    u = boost::get<boost::uuids::uuid>(it->rowkey_.at(0));
                } catch (boost::bad_get& ex) {
                    QE_LOG(ERROR, "Invalid rowkey/uuid");
                    QE_IO_ERROR_RETURN(0, QUERY_FAILURE);
                }
                QE_LOG(ERROR, "No entry for uuid: " << UuidToString(u) <<
                       " in Analytics db");
                continue;
            }

            std::map<std::string, std::string> cmap;
            std::vector<std::string>::iterator jt;
            for (jt = select_column_fields.begin();
                 jt != select_column_fields.end(); jt++) {
                std::map<std::string, GenDb::DbDataValue>::iterator kt = col_res_map.find(*jt);
                if (kt == col_res_map.end()) {
                    if (m_query->is_object_table_query()) {
                        if (process_object_query_specific_select_params(
                                        *jt, col_res_map, cmap) == false) {
                            // Exit the loop. User is not interested 
                            // in this object log. 
                            break;
                        }
                    } else {
                        // do not assert, append an empty string
                        cmap.insert(std::make_pair(*jt, std::string("")));
                    }
                } else if (*jt == g_viz_constants.UUID_KEY) {

                    boost::uuids::uuid u;
                    assert(it->rowkey_.size() > 0);
                    try {
                        u = boost::get<boost::uuids::uuid>(it->rowkey_.at(0));
                    } catch (boost::bad_get& ex) {
                        QE_ASSERT(0);
                    }
                    std::string u_s(u.size(), 0);
                    std::copy(u.begin(), u.end(), u_s.begin());

                    cmap.insert(std::make_pair(kt->first, u_s));
                } else {
                    std::string vstr;
                    switch (kt->second.which()) {
                    case GenDb::DB_VALUE_STRING: {
                        vstr = boost::get<std::string>(kt->second);
                        break;
                    }
                    case GenDb::DB_VALUE_UINT64: {
                        uint64_t vint = boost::get<uint64_t>(kt->second);
                        vstr = integerToString(vint);
                        break;
                    }     
                    case GenDb::DB_VALUE_UINT32: {
                        uint32_t vint = boost::get<uint32_t>(kt->second);
                        vstr = integerToString(vint);
                        break;
                    }     
                    case GenDb::DB_VALUE_UINT16: {
                        uint16_t vint = boost::get<uint16_t>(kt->second);
                        vstr = integerToString(vint);
                        break;
                    }     
                    case GenDb::DB_VALUE_UINT8: {
                        uint8_t vint = boost::get<uint8_t>(kt->second);
                        vstr = integerToString(vint);
                        break;
                    }     
                    case GenDb::DB_VALUE_UUID: {
                        boost::uuids::uuid vuuid = boost::get<boost::uuids::uuid>(kt->second);
                        vstr = to_string(vuuid); 
                        break;
                    }
                    case GenDb::DB_VALUE_DOUBLE: {
                        double vdouble = boost::get<double>(kt->second);
                        vstr = integerToString(vdouble);
                        break;
                    } 
                    default:
                        QE_ASSERT(0);
                        break;
                    }
                    cmap.insert(std::make_pair(kt->first, vstr));
                } 
            }
            if (jt == select_column_fields.end()) {
                result_->push_back(std::make_pair(cmap, nullmetadata));
            } 
        }
    }

    // Have the result ready and processing is done
    status_details = 0;
    parent_query->subquery_processed(this);
    return QUERY_SUCCESS;
}

bool SelectQuery::process_object_query_specific_select_params(
                        const std::string& sel_field,
                        std::map<std::string, GenDb::DbDataValue>& col_res_map,
                        std::map<std::string, std::string>& cmap) {
    std::map<std::string, GenDb::DbDataValue>::iterator cit;
    cit = col_res_map.find(g_viz_constants.SANDESH_TYPE);
    QE_ASSERT(cit != col_res_map.end());
    uint8_t type_val;
    try {
        type_val = boost::get<uint8_t>(cit->second);
    } catch (boost::bad_get& ex) {
        QE_ASSERT(0);
    }

    std::string sandesh_type;
    if (type_val == SandeshType::SYSTEM) {
        sandesh_type = g_viz_constants.SYSTEM_LOG;
    } else if ((type_val == SandeshType::OBJECT) || (type_val == SandeshType::UVE)) {
        sandesh_type = g_viz_constants.OBJECT_LOG;
    } else {
        // Ignore this message.
        return false;
    }

    if (sel_field == sandesh_type) {
        std::map<std::string, GenDb::DbDataValue>::iterator xml_it;
        xml_it = col_res_map.find(g_viz_constants.DATA);
        QE_ASSERT(xml_it != col_res_map.end());
        std::string xml_data;
        try {
            xml_data = boost::get<std::string>(xml_it->second);
        } catch (boost::bad_get& ex) {
            QE_ASSERT(0);
        }
        cmap.insert(std::make_pair(sel_field, xml_data));
    } else if (is_present_in_select_column_fields(sandesh_type)) {
        cmap.insert(std::make_pair(sel_field, std::string("")));
    } else {
        return false;
    }

    return true;
}

