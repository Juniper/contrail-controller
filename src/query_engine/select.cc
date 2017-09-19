/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "query.h"

#include "boost/uuid/uuid_io.hpp"
#include "boost/algorithm/string.hpp"
#include "base/util.h"
#include "rapidjson/document.h"
#include "analytics/viz_types.h"
#include "analytics/viz_constants.h"
#include "analytics/vizd_table_desc.h"
#include "analytics/db_handler_impl.h"
#include "stats_select.h"

using std::string;

std::vector<std::string> session_json_fields = boost::assign::list_of
    ("remote_ip")
    ("client_port")
    ("other_vrouter_ip")
    ("underlay_proto")
    ("forward_flow_uuid")
    ("forward_flow_setup_time")
    ("forward_flow_teardown_time")
    ("forward_flow_action")
    ("forward_flow_sg_rule_uuid")
    ("forward_flow_nw_ave_uuid")
    ("forward_flow_underlay_source_port")
    ("forward_flow_drop_reason")
    ("forward_flow_teardown_bytes")
    ("forward_flow_teardown_pkts")
    ("reverse_flow_uuid")
    ("reverse_flow_setup_time")
    ("reverse_flow_teardown_time")
    ("reverse_flow_action")
    ("reverse_flow_sg_rule_uuid")
    ("reverse_flow_nw_ave_uuid")
    ("reverse_flow_underlay_source_port")
    ("reverse_flow_drop_reason")
    ("reverse_flow_teardown_bytes")
    ("reverse_flow_teardown_pkts")
    ("sourceip")
    ("destip")
    ("dport")
    ("sport")
    ("UuidKey")
    ("setup_time")
    ("teardown_time")
    ("agg-bytes")
    ("agg-pkts")
    ("action")
    ("sg_rule_uuid")
    ("nw_ace_uuid")
    ("underlay_source_port")
    ("drop_reason");

SelectQuery::SelectQuery(QueryUnit *main_query,
        const std::map<std::string, std::string>& json_api_data):
    QueryUnit(main_query, main_query),
    provide_timeseries(false),
    granularity(0),
    unroll_needed(false),
    fs_query_type_(SelectQuery::FS_SELECT_INVALID) {

    AnalyticsQuery *m_query = (AnalyticsQuery *)main_query;
    result_.reset(new BufT);
    mresult_.reset(new MapBufT);

    // initialize Cassandra related fields
    if (
            (m_query->table() == g_viz_constants.COLLECTOR_GLOBAL_TABLE) ||
            (m_query->is_object_table_query(m_query->table()))
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
    std::map<std::string, std::string>::const_iterator iter;
    iter = json_api_data.find(QUERY_SELECT);
    QE_PARSE_ERROR(iter != json_api_data.end());

    contrail_rapidjson::Document d;
    std::string json_string = "{ \"select\" : " + 
        iter->second + " }";
    json_string_ = json_string;
    QE_TRACE(DEBUG, "parsing through rapidjson: " << json_string);
    d.Parse<0>(const_cast<char *>(json_string.c_str()));
    const contrail_rapidjson::Value& json_select_fields = d["select"]; 
    QE_PARSE_ERROR(json_select_fields.IsArray());
    QE_TRACE(DEBUG, "# of select fields is " << json_select_fields.Size());

    // whether uuid key was provided in select field
    bool uuid_key_selected = false;
    bool reverse_uuid_key_selected = false;

    if (m_query->is_stat_table_query(m_query->table())
           || m_query->is_session_query(m_query->table())) {
        for (contrail_rapidjson::SizeType i = 0; i < json_select_fields.Size(); i++) {
            std::string field(json_select_fields[i].GetString());
            if (field == SELECT_SESSION_CLASS_ID) {
                try {
                    select_column_fields.push_back("CLASS(" +
                                                select_column_fields[0] + ")");
                } catch (std::out_of_range& e) {
                    QE_INVALIDARG_ERROR(0);
                }
            } else if (field == SELECT_SAMPLE_COUNT) {
                try {
                    select_column_fields.push_back("COUNT(" +
                                                select_column_fields[0] + ")");
                } catch (std::out_of_range& e) {
                    QE_INVALIDARG_ERROR(0);
                }
            } else {
                if (field == "forward_flow_uuid") {
                    uuid_key_selected = true;
                }
                if (field == "reverse_flow_uuid") {
                    reverse_uuid_key_selected = true;
                }
                select_column_fields.push_back(field);
            }
            std::vector<std::string>::iterator it =
                std::find(session_json_fields.begin(),
                            session_json_fields.end(),
                            field);
                unroll_needed |= (it != session_json_fields.end());
        }
        
        if (m_query->table() == g_viz_constants.SESSION_RECORD_TABLE) {
            if (!uuid_key_selected) {
                select_column_fields.push_back(g_viz_constants.FORWARD_FLOW_UUID);
            }
            if (!reverse_uuid_key_selected) {
                select_column_fields.push_back(g_viz_constants.REVERSE_FLOW_UUID);
            }
            unroll_needed = true;
        }
        stats_.reset(new StatsSelect(m_query, select_column_fields));
        QE_INVALIDARG_ERROR(stats_->Status());
        return;
    }     

    for (contrail_rapidjson::SizeType i = 0; i < json_select_fields.Size(); i++) 
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
            QE_INVALIDARG_ERROR(m_query->is_flow_query(m_query->table()) ||
                m_query->is_stat_table_query(m_query->table()));
        } else if (boost::starts_with(json_select_fields[i].GetString(),
                    TIMESTAMP_GRANULARITY))
        {
            provide_timeseries = true;
            std::string timestamp_str = json_select_fields[i].GetString();
            granularity = atoi(timestamp_str.c_str() + 
                + sizeof(TIMESTAMP_GRANULARITY)-1);
            granularity = granularity * kMicrosecInSec;
#ifndef USE_SESSION
            select_column_fields.push_back(TIMESTAMP_GRANULARITY);
#else
            select_column_fields.push_back(json_select_fields[i].GetString());
#endif
            QE_INVALIDARG_ERROR(
                m_query->table() == g_viz_constants.FLOW_SERIES_TABLE ||
                m_query->is_stat_table_query(m_query->table()));
        } 
        else if (json_select_fields[i].GetString() ==
                std::string(SELECT_PACKETS)) {
            agg_stats_t agg_stats_entry = {RAW, PKT_STATS};
            agg_stats.push_back(agg_stats_entry);
#ifdef USE_SESSION
            select_column_fields.push_back(SELECT_PACKETS);
#endif
            QE_INVALIDARG_ERROR(m_query->is_flow_query(m_query->table()));
        }
        else if (json_select_fields[i].GetString() ==
                std::string(SELECT_BYTES)) {
            agg_stats_t agg_stats_entry = {RAW, BYTE_STATS};
            agg_stats.push_back(agg_stats_entry);
#ifdef USE_SESSION
            select_column_fields.push_back(SELECT_BYTES);
#endif
            QE_INVALIDARG_ERROR(m_query->is_flow_query(m_query->table()));
        }
        else if (json_select_fields[i].GetString() ==
                std::string(SELECT_SUM_PACKETS)) {
            agg_stats_t agg_stats_entry = {SUM, PKT_STATS};
            agg_stats.push_back(agg_stats_entry);
#ifdef USE_SESSION
            select_column_fields.push_back(SELECT_SUM_PACKETS);
#endif

            QE_INVALIDARG_ERROR(
                m_query->table() == g_viz_constants.FLOW_SERIES_TABLE);
        }
        else if (json_select_fields[i].GetString() ==
                std::string(SELECT_SUM_BYTES)) {
            agg_stats_t agg_stats_entry = {SUM, BYTE_STATS};
            agg_stats.push_back(agg_stats_entry);
#ifdef USE_SESSION
            select_column_fields.push_back(SELECT_SUM_BYTES);
#endif
            QE_INVALIDARG_ERROR(
                m_query->table() == g_viz_constants.FLOW_SERIES_TABLE);
        }
        else if (json_select_fields[i].GetString() ==
                std::string(SELECT_FLOW_CLASS_ID)) {
#ifndef USE_SESSION
            select_column_fields.push_back(SELECT_FLOW_CLASS_ID);
#else
            select_column_fields.push_back("CLASS(" +
                                            select_column_fields[0] + ")");
#endif
            QE_INVALIDARG_ERROR(
                m_query->table() == g_viz_constants.FLOW_SERIES_TABLE);
        }
        else if (json_select_fields[i].GetString() ==
                std::string(SELECT_FLOW_COUNT)) {
#ifndef USE_SESSION
            select_column_fields.push_back(SELECT_FLOW_COUNT);
#else
            QE_INVALIDARG_ERROR(0);
#endif
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
#ifdef USE_SESSION
            std::vector<std::string>::iterator it =
                std::find(session_json_fields.begin(),
                            session_json_fields.end(),
                            json_select_fields[i].GetString());
                std::string field = json_select_fields[i].GetString();
                if ((m_query->wherequery_->direction_ing == 1 && field == "sourceip")
                    || (m_query->wherequery_->direction_ing == 0 && field == "destip")) {
                    unroll_needed |= false;
                } else {
                    unroll_needed |= (it != session_json_fields.end());
                }
#endif
        }
    }

    if (m_query->table() == g_viz_constants.FLOW_SERIES_TABLE) {
        evaluate_fs_query_type();
        if (fs_query_type_ == SelectQuery::FS_SELECT_INVALID)
            QE_INVALIDARG_ERROR(false);
    }

    if ((m_query->table() == g_viz_constants.FLOW_TABLE) && !uuid_key_selected) {
        select_column_fields.push_back(g_viz_constants.UUID_KEY);
    }

#ifdef USE_SESSION
    if (m_query->is_flow_query(m_query->table())) {
        stats_.reset(new StatsSelect(m_query, select_column_fields));
        QE_INVALIDARG_ERROR(stats_->Status());
        if (!m_query->wherequery_->additional_select_.empty()) {
            // add additional select fields based on filters
            select_column_fields.insert(select_column_fields.end(),
                m_query->wherequery_->additional_select_.begin(),
                m_query->wherequery_->additional_select_.end());
        }
    }
#endif

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

    return true;
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
        agg_op_t stats_op = AGG_OP_INVALID;
        std::vector<agg_stats_t>::const_iterator it;
        for (it = agg_stats.begin(); it != agg_stats.end(); ++it) {
            if ((*it).agg_op == RAW) {
                if ((fs_query_type_ & SelectQuery::FS_SELECT_TS) ||
                    (stats_op == SUM)) {
                    fs_query_type_ = SelectQuery::FS_SELECT_INVALID;
                    return;
                }
                stats_op = RAW;
                fs_query_type_ |= SelectQuery::FS_SELECT_T;
            } else if ((*it).agg_op == SUM) {
                if ((fs_query_type_ & SelectQuery::FS_SELECT_T) ||
                    (stats_op == RAW)) {
                    fs_query_type_ = SelectQuery::FS_SELECT_INVALID;
                    return;
                }
                stats_op = SUM;
            }
        }
    }
}

class SessionTableAttributeConverter : public boost::static_visitor<> {
  public:
    SessionTableAttributeConverter(std::vector<StatsSelect::StatEntry> *attribs):
       attribs_(attribs),
       cass_column2column_name_map_((g_viz_constants._VIZD_SESSION_TABLE_SCHEMA
                                .find(g_viz_constants.SESSION_TABLE))->second
                                .column_to_query_column) {
    }
    void operator()(const boost::blank &tblank, const int idx) const {
        QE_ASSERT(false && "Null Value in Query Result");
    }
    void operator()(const std::string &tstring, const int idx) const {
        if (tstring.empty()) {
            return;
        }
        StatsSelect::StatEntry se;
        std::string cname = "column" + integerToString(idx);
        std::map<std::string, std::string>::const_iterator itr;
        itr = (cass_column2column_name_map_.find(cname));
        QE_ASSERT(itr != cass_column2column_name_map_.end());
        se.name = itr->second;
        ColIndexType::type index_type =
            g_viz_constants._VIZD_SESSION_TABLE_SCHEMA.find(
            g_viz_constants.SESSION_TABLE)->second.columns[idx].index_type;
        if (index_type) {
            //boost::regex expr("^[\\d]+:");
            std::string value(boost::regex_replace(tstring,
                SessionTableAttributeConverter::t2_expr_, "",
                    boost::match_default | boost::format_all));
            se.value = value;
        } else {
            se.value = tstring;
        }
        attribs_->push_back(se);
    }
    void operator()(const boost::uuids::uuid &tuuid, const int idx) const {
        StatsSelect::StatEntry se;
        std::string cname = "column" + integerToString(idx);
        std::map<std::string, std::string>::const_iterator itr;
        itr = (cass_column2column_name_map_.find(cname));
        QE_ASSERT(itr != cass_column2column_name_map_.end());
        se.name = itr->second;
        se.value = to_string(tuuid);
        attribs_->push_back(se);
    }
    template <typename IntegerType>
    void operator()(const IntegerType &num, const int idx) const {
        StatsSelect::StatEntry se;
        std::string cname = "column" + integerToString(idx);
        std::map<std::string, std::string>::const_iterator itr;
        itr = (cass_column2column_name_map_.find(cname));
        QE_ASSERT(itr != cass_column2column_name_map_.end());
        se.name = itr->second;
        se.value = (uint64_t)num;
        attribs_->push_back(se);
    }
    void operator()(const double &tdouble, const int idx) const {
        StatsSelect::StatEntry se;
        std::string cname = "column" + integerToString(idx);
        std::map<std::string, std::string>::const_iterator itr;
        itr = (cass_column2column_name_map_.find(cname));
        QE_ASSERT(itr != cass_column2column_name_map_.end());
        se.name = itr->second;
        se.value = tdouble;
        attribs_->push_back(se);
    }
    void operator()(const IpAddress &tipaddr, const int idx) const {
        StatsSelect::StatEntry se;
        std::string cname = "column" + integerToString(idx);
        std::map<std::string, std::string>::const_iterator itr;
        itr = (cass_column2column_name_map_.find(cname));
        QE_ASSERT(itr != cass_column2column_name_map_.end());
        se.name = itr->second;
        se.value = tipaddr.to_string();
        attribs_->push_back(se);
    }
    void operator()(const GenDb::Blob &tblob, const int idx) const {
        T2IpIndex t2_ip;
        memcpy(&t2_ip, tblob.data(), tblob.size());
        StatsSelect::StatEntry se;
        std::string cname = "column" + integerToString(idx);
        std::map<std::string, std::string>::const_iterator itr;
        itr = (cass_column2column_name_map_.find(cname));
        QE_ASSERT(itr != cass_column2column_name_map_.end());
        se.name = itr->second;
        se.value = t2_ip.ip_.to_string();
        attribs_->push_back(se);
    }
private:
    std::vector<StatsSelect::StatEntry> *attribs_;
    std::map<std::string, std::string> cass_column2column_name_map_;
    static boost::regex t2_expr_;
};

boost::regex SessionTableAttributeConverter::t2_expr_("^[\\d]+:");

void populate_attribs_from_json_member(
    const contrail_rapidjson::Value::ConstMemberIterator itr,
    std::vector<StatsSelect::StatEntry> *attribs_, const std::string& prefix) {
    QE_ASSERT(itr->name.IsString());

    std::string fvname(itr->name.GetString());

    StatsSelect::StatEntry se;
    se.name = prefix + fvname;
    if (itr->value.IsString()) {
        se.value = itr->value.GetString();
    } else if (itr->value.IsUint()) {
        se.value = (uint64_t)itr->value.GetUint();
    } else if (itr->value.IsUint64()){
        se.value = (uint64_t)itr->value.GetUint64();
    } else if (itr->value.IsDouble()) {
        se.value = (double) itr->value.GetDouble();
    } else {
        QE_ASSERT(0);
    }
    attribs_->push_back(se);
}

void parse_json(const contrail_rapidjson::Value &json_object,
    std::vector<StatsSelect::StatEntry> *attribs_) {
    for (contrail_rapidjson::Value::ConstMemberIterator itr =
        json_object.MemberBegin(); itr != json_object.MemberEnd(); ++itr) {
        QE_ASSERT(itr->name.IsString());
        if (itr->value.IsObject()) {
            std::string prefix;
            if (itr->name == "forward_flow_info") {
                prefix = "forward_";
            } else {
                prefix = "reverse_";
            }
            for (contrail_rapidjson::Value::ConstMemberIterator itr2 =
                itr->value.MemberBegin(); itr2 != itr->value.MemberEnd(); ++itr2) {
                populate_attribs_from_json_member(itr2, attribs_, prefix);
            }
        } else {
            populate_attribs_from_json_member(itr, attribs_, "");
        }
    }
}

void map_session_to_flow(std::vector<StatsSelect::StatEntry> *attribs_,
    uint8_t is_client_session, uint8_t direction) {
    std::map<std::string, std::string> session2flow_map;
    if (direction) {
        if (is_client_session) {
            session2flow_map = g_viz_constants.session2flow_maps[0];
        } else {
            session2flow_map = g_viz_constants.session2flow_maps[2];
        }
    } else {
        if (is_client_session) {
            session2flow_map = g_viz_constants.session2flow_maps[1];
        } else {
            session2flow_map = g_viz_constants.session2flow_maps[3];
        }
    }

    std::vector<StatsSelect::StatEntry>::iterator it;
    for (it = attribs_->begin(); it != attribs_->end(); it++) {
        std::map<std::string, std::string>::iterator itr =
                session2flow_map.find(it->name);
        if (itr != session2flow_map.end()) {
            it->name = itr->second;
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
     *bn
    the next level
     *
     *  message related queries
     */
    AnalyticsQuery *m_query = (AnalyticsQuery *)main_query;
    const std::vector<query_result_unit_t>& query_result =
        *m_query->where_info_;
    boost::shared_ptr<QueryResultMetaData> nullmetadata;

#ifndef USE_SESSION
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

        for (std::vector<query_result_unit_t>::const_iterator it = query_result.begin();
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
        if (!m_query->dbif_->Db_GetMultiRow(&mget_res, g_viz_constants.FLOW_TABLE, keys)) {
            QE_IO_ERROR_RETURN(0, QUERY_FAILURE);
        }

        std::vector<GenDb::NewCf>::const_iterator fit;
        for (fit = vizd_flow_tables.begin(); fit != vizd_flow_tables.end(); fit++) {
            if (fit->cfname_ == g_viz_constants.FLOW_TABLE)
                break;
        }
        if (fit == vizd_flow_tables.end())
            VIZD_ASSERT(0);
        const GenDb::NewCf::ColumnMap& sql_cols = fit->cfcolumns_;
        GenDb::NewCf::ColumnMap::const_iterator col_type_it;

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
                    cmap.insert(std::make_pair(*jt, std::string("")));
                    continue;
                }

                if ((col_type_it = sql_cols.find(kt->first)) == sql_cols.end()) {
                    // valid assert, this is a bug
                    QE_ASSERT(0);
                }

                const GenDb::DbDataValue &db_value(kt->second);
                std::string elem_value(GenDb::DbDataValueToString(db_value));
                cmap.insert(std::make_pair(kt->first, elem_value));
            }
            result_->push_back(std::make_pair(cmap, nullmetadata));
        }
    }
    else if (m_query->is_session_query(m_query->table())) {
#else
    if (m_query->is_session_query(m_query->table())
              || m_query->is_flow_query(m_query->table())) {
#endif
        QE_ASSERT(stats_.get());
        // can not handle query result of huge size
        if (query_result.size() > (size_t)query_result_size_limit) {
            QE_LOG(DEBUG,
            "Can not handle query result of size:" << query_result.size());
            QE_IO_ERROR_RETURN(0, QUERY_FAILURE);
        }

        if (!unroll_needed) {
            for (std::vector<query_result_unit_t>::const_iterator it = query_result.begin();
                    it != query_result.end(); it++) {
                boost::uuids::uuid u;
                GenDb::DbDataValueVec::const_iterator itr;
                int idx = 2;
                uint8_t session_type = boost::get<uint8_t>(it->info.at(1));
                std::vector<StatsSelect::StatEntry> attribs;
                SessionTableAttributeConverter session_attribs_builder(&attribs);
                for (itr = it->info.begin(); itr != it->info.end(); ++itr) {
                    if (idx == SessionRecordFields::SESSION_T1) {
                        idx++;
                        continue;
                    }
                    if (idx == SessionRecordFields::SESSION_UUID) {
                        u = boost::get<boost::uuids::uuid>(*itr);
                        idx++;
                        continue;
                    }
                    const GenDb::DbDataValue &db_value(*itr);
                    boost::apply_visitor(boost::bind(session_attribs_builder, _1,
                        g_viz_constants.SessionCassTableColumns[idx]), db_value);
                    ++idx;
                }
                if (m_query->is_flow_query(m_query->table())) {
                    map_session_to_flow(&attribs, session_type,
                                    m_query->wherequery_->direction_ing);
                }
                stats_->LoadRow(u, it->timestamp, attribs, *mresult_);
            }
        } else {
            uint64_t parset=0;
            uint64_t loadt=0;
            uint64_t jsont=0;
            for (std::vector<query_result_unit_t>::const_iterator it =
                    query_result.begin(); it != query_result.end(); it++) {
                boost::uuids::uuid u;
                GenDb::DbDataValueVec::const_iterator itr;
                int idx = 2;
                uint8_t session_type = boost::get<uint8_t>(it->info.at(1));
                std::vector<StatsSelect::StatEntry> attribs;
                SessionTableAttributeConverter session_attribs_builder(&attribs);
                for (itr = it->info.begin(); itr != (it->info.end() - 1); ++itr) {
                    if (g_viz_constants.SessionCassTableColumns[idx] ==
                        SessionRecordFields::SESSION_T1) {
                        idx++;
                        continue;
                    }
                    if (g_viz_constants.SessionCassTableColumns[idx] ==
                        SessionRecordFields::SESSION_SAMPLED_FORWARD_BYTES) {
                        idx += g_viz_constants.NUM_SESSION_STATS_FIELDS;
                        itr += g_viz_constants.NUM_SESSION_STATS_FIELDS - 1;
                        continue;
                    }
                    if (g_viz_constants.SessionCassTableColumns[idx] ==
                        SessionRecordFields::SESSION_UUID) {
                        u = boost::get<boost::uuids::uuid>(*itr);
                        idx++;
                        continue;
                    }
                    const GenDb::DbDataValue &db_value(*itr);
                    boost::apply_visitor(boost::bind(session_attribs_builder, _1,
                        g_viz_constants.SessionCassTableColumns[idx]), db_value);
                    ++idx;
                }
                std::string session_map(boost::get<std::string>(*itr));
                contrail_rapidjson::Document d;
                uint64_t thenj = UTCTimestampUsec();
                if (d.Parse<0>(const_cast<char *>(
                        session_map.c_str())).HasParseError()) {
                    QE_LOG(ERROR, "Error parsing json document: " <<
                           d.GetParseError() << " - " << session_map);
                    continue;
                }
                jsont += UTCTimestampUsec() - thenj;
                for (contrail_rapidjson::Value::ConstMemberIterator itr2 =
                    d.MemberBegin(); itr2 != d.MemberEnd(); ++itr2) {
                    QE_ASSERT(itr2->name.IsString());
                    uint64_t thenp = UTCTimestampUsec();
                    std::string ip_port(itr2->name.GetString());
                    size_t delim_idx = ip_port.find(":");
                    {
                        StatsSelect::StatEntry se_client_port;
                        se_client_port.name = "client_port";
                        se_client_port.value = ip_port.substr(0, delim_idx);
                        attribs.push_back(se_client_port);
                    }
                    {
                        StatsSelect::StatEntry se_remote_ip;
                        se_remote_ip.name = "remote_ip";
                        se_remote_ip.value = ip_port.substr(delim_idx + 1);
                        attribs.push_back(se_remote_ip);
                    }
                    parse_json(itr2->value, &attribs);
                    parset += UTCTimestampUsec() - thenp;
                    uint64_t thenl = UTCTimestampUsec();
                    if (m_query->is_flow_query(m_query->table())) {
                        map_session_to_flow(&attribs, session_type,
                            m_query->wherequery_->direction_ing);
                    }
                    stats_->LoadRow(u, it->timestamp, attribs, *mresult_);
                    loadt += UTCTimestampUsec() - thenl;
                }
            }
            QE_TRACE(DEBUG, "Select ProcTime - Entries : " << query_result.size() <<
                " json : " << jsont << " parse : " << parset << " load : " << loadt);

        }
    } else if (m_query->is_stat_table_query(m_query->table())) {
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
        for (std::vector<query_result_unit_t>::const_iterator it = query_result.begin();
                it != query_result.end(); it++) {

            string json_string;
            GenDb::DbDataValue value;
            boost::uuids::uuid u;

            it->get_stattable_info(json_string, u);

            //uint64_t thenj = UTCTimestampUsec();
            contrail_rapidjson::Document d;
            if (d.Parse<0>(const_cast<char *>(
                    json_string.c_str())).HasParseError()) {
                QE_LOG(ERROR, "Error parsing json document: " <<
                       d.GetParseError() << " - " << json_string);
                continue;
            }
            //jsont += UTCTimestampUsec() - thenj;

            std::vector<StatsSelect::StatEntry> attribs;
            {
                for (contrail_rapidjson::Value::ConstMemberIterator itr = d.MemberBegin();
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
                        if (itr->value.IsUint()) {
                            se.value = (uint64_t)itr->value.GetUint();
                        } else {
                            se.value = (uint64_t)itr->value.GetUint64();
                        }
                    } else if (tname == 'd') {
                        se.value = (double) itr->value.GetDouble();
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
        if (!m_query->dbif_->Db_GetMultiRow(&mget_res, g_viz_constants.OBJECT_VALUE_TABLE, keys)) {
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

         /*
          * In case of objecttable queries, if object_id is also part of
          * select field, then construct a map between the object_id and
          * uuid and populate the select field based on this map
          */
        std::map<boost::uuids::uuid, std::string> uuid_to_object_id;
        for (std::vector<query_result_unit_t>::const_iterator it = query_result.begin();
                it != query_result.end(); it++) {
            boost::uuids::uuid u;

            it->get_uuid(u);
            if (m_query->is_object_table_query(m_query->table())) {
                std::string object_id;
                it->get_objectid(object_id);
                uuid_to_object_id.insert(std::make_pair(u, object_id));
            }
            GenDb::DbDataValueVec a_key;
            a_key.push_back(u);
            keys.push_back(a_key);
        }

        GenDb::ColListVec mget_res;
        if (!m_query->dbif_->Db_GetMultiRow(&mget_res,
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
            boost::uuids::uuid uuid_rkey;
            if (!col_res_map.size()) {
                assert(it->rowkey_.size() > 0);
                try {
                    uuid_rkey = boost::get<boost::uuids::uuid>(it->rowkey_.at(0));
                } catch (boost::bad_get& ex) {
                    QE_LOG(ERROR, "Invalid rowkey/uuid");
                    QE_IO_ERROR_RETURN(0, QUERY_FAILURE);
                }
                QE_LOG(ERROR, "No entry for uuid: " << UuidToString(uuid_rkey) <<
                       " in Analytics db");
                continue;
            } else {
                try {
                    uuid_rkey = boost::get<boost::uuids::uuid>(it->rowkey_.at(0));
                } catch (boost::bad_get& ex) {
                    QE_LOG(ERROR, "Invalid rowkey/uuid");
                    QE_IO_ERROR_RETURN(0, QUERY_FAILURE);
                }
            }

            std::map<std::string, std::string> cmap;
            std::vector<std::string>::iterator jt;
            for (jt = select_column_fields.begin();
                 jt != select_column_fields.end(); jt++) {
                std::map<std::string, GenDb::DbDataValue>::iterator kt = col_res_map.find(*jt);
                if (kt == col_res_map.end()) {
                    if (m_query->is_object_table_query(m_query->table())) {
                        if (process_object_query_specific_select_params(
                            *jt, col_res_map, cmap, uuid_rkey,
                            uuid_to_object_id ) == false) {
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
                    std::string vstr(GenDb::DbDataValueToString(kt->second));
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
                        std::map<std::string, std::string>& cmap,
                        const boost::uuids::uuid& uuid,
                        std::map<boost::uuids::uuid, std::string>&
                        uuid_to_objectid) {
    std::map<std::string, GenDb::DbDataValue>::iterator cit;
    cit = col_res_map.find(g_viz_constants.SANDESH_TYPE);
    QE_ASSERT(cit != col_res_map.end());
    uint32_t type_val;
    try {
        type_val = boost::get<uint32_t>(cit->second);
    } catch (boost::bad_get& ex) {
        QE_ASSERT(0);
    }

    std::string sandesh_type;
    if (type_val == SandeshType::SYSTEM) {
        sandesh_type = g_viz_constants.SYSTEM_LOG;
    } else if ((type_val == SandeshType::OBJECT) ||
               (type_val == SandeshType::UVE) ||
               (type_val == SandeshType::ALARM)) {
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
    } else if (sel_field == "ObjectId") {
        // Look up the object_id corresponding to the uuid
        std::map<boost::uuids::uuid, std::string>::iterator uuid_iter;
        uuid_iter = uuid_to_objectid.find(uuid);
        QE_ASSERT(uuid_iter != uuid_to_objectid.end());
        std::string object_id_val(uuid_iter->second);
        cmap.insert(std::make_pair(sel_field,
            object_id_val));
    } else if (is_present_in_select_column_fields(sandesh_type)) {
        cmap.insert(std::make_pair(sel_field, std::string("")));
    } else {
        return false;
    }

    return true;
}

