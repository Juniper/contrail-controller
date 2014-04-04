/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/util.h"
#include "query.h"

SelectQuery::process_fs_query_cb_map_t SelectQuery::process_fs_query_cb_map_ =
    SelectQuery::process_fs_query_cb_map_init();  
SelectQuery::populate_fs_result_cb_map_t SelectQuery::populate_fs_result_cb_map_ =
    SelectQuery::populate_fs_result_cb_map_init(); 

SelectQuery::process_fs_query_cb_map_t SelectQuery::process_fs_query_cb_map_init() {
    process_fs_query_cb_map_t query_cb_map;
    query_cb_map[FS_SELECT_T] = 
        &SelectQuery::process_fs_query_with_time_tuple_stats_fields;
    query_cb_map[FS_SELECT_TS] = 
        &SelectQuery::process_fs_query_with_ts;
    query_cb_map[FS_SELECT_FLOW_TUPLE] = 
        &SelectQuery::process_fs_query_with_time_tuple_stats_fields;
    query_cb_map[FS_SELECT_STATS] = 
        &SelectQuery::process_fs_query_with_stats_fields;
    query_cb_map[FS_SELECT_T_FLOW_TUPLE] = 
        &SelectQuery::process_fs_query_with_time_tuple_stats_fields;
    query_cb_map[FS_SELECT_T_STATS] = 
        &SelectQuery::process_fs_query_with_time_tuple_stats_fields;
    query_cb_map[FS_SELECT_FLOW_TUPLE_STATS] = 
        &SelectQuery::process_fs_query_with_tuple_stats_fields;
    query_cb_map[FS_SELECT_TS_FLOW_TUPLE] = 
        &SelectQuery::process_fs_query_with_ts_tuple_fields;
    query_cb_map[FS_SELECT_TS_STATS] = 
        &SelectQuery::process_fs_query_with_ts_stats_fields;
    query_cb_map[FS_SELECT_T_FLOW_TUPLE_STATS] = 
        &SelectQuery::process_fs_query_with_time_tuple_stats_fields;
    query_cb_map[FS_SELECT_TS_FLOW_TUPLE_STATS] = 
        &SelectQuery::process_fs_query_with_ts_tuple_stats_fields;

    return query_cb_map;
}

SelectQuery::populate_fs_result_cb_map_t SelectQuery::populate_fs_result_cb_map_init() {
    populate_fs_result_cb_map_t result_cb_map;
    result_cb_map[FS_SELECT_T] = 
        &SelectQuery::populate_fs_query_result_with_time_tuple_stats_fields;
    result_cb_map[FS_SELECT_TS] = 
        &SelectQuery::populate_fs_query_result_with_ts;
    result_cb_map[FS_SELECT_FLOW_TUPLE] = 
        &SelectQuery::populate_fs_query_result_with_time_tuple_stats_fields;
    result_cb_map[FS_SELECT_STATS] = 
        &SelectQuery::populate_fs_query_result_with_stats_fields;
    result_cb_map[FS_SELECT_T_FLOW_TUPLE] = 
        &SelectQuery::populate_fs_query_result_with_time_tuple_stats_fields;
    result_cb_map[FS_SELECT_T_STATS] = 
        &SelectQuery::populate_fs_query_result_with_time_tuple_stats_fields;
    result_cb_map[FS_SELECT_FLOW_TUPLE_STATS] = 
        &SelectQuery::populate_fs_query_result_with_tuple_stats_fields;
    result_cb_map[FS_SELECT_TS_FLOW_TUPLE] = 
        &SelectQuery::populate_fs_query_result_with_ts_tuple_fields;
    result_cb_map[FS_SELECT_TS_STATS] = 
        &SelectQuery::populate_fs_query_result_with_ts_stats_fields;
    result_cb_map[FS_SELECT_T_FLOW_TUPLE_STATS] = 
        &SelectQuery::populate_fs_query_result_with_time_tuple_stats_fields;
    result_cb_map[FS_SELECT_TS_FLOW_TUPLE_STATS] = 
        &SelectQuery::populate_fs_query_result_with_ts_tuple_stats_fields;
    
    return result_cb_map; 
}

void SelectQuery::get_flow_class(const flow_tuple& tuple, flow_tuple& flowclass) {
    std::vector<std::string>::const_iterator it;
    for (it = select_column_fields.begin(); 
         it != select_column_fields.end(); ++it) {
        std::string qstring(get_query_string(*it));
        if (qstring == g_viz_constants.FlowRecordNames[FlowRecordFields::FLOWREC_VROUTER]) {
            flowclass.vrouter = tuple.vrouter;
        }  else if (qstring == g_viz_constants.FlowRecordNames[FlowRecordFields::FLOWREC_SOURCEVN]) {
            flowclass.source_vn = tuple.source_vn;
        } else if (qstring == g_viz_constants.FlowRecordNames[FlowRecordFields::FLOWREC_SOURCEIP]) {
            flowclass.source_ip = tuple.source_ip;
        } else if (qstring == g_viz_constants.FlowRecordNames[FlowRecordFields::FLOWREC_DESTVN]) {
            flowclass.dest_vn = tuple.dest_vn;
        } else if (qstring == g_viz_constants.FlowRecordNames[FlowRecordFields::FLOWREC_DESTIP]) {
            flowclass.dest_ip = tuple.dest_ip;
        } else if (qstring == g_viz_constants.FlowRecordNames[FlowRecordFields::FLOWREC_PROTOCOL]) {
            flowclass.protocol = tuple.protocol;
        } else if (qstring == g_viz_constants.FlowRecordNames[FlowRecordFields::FLOWREC_SPORT]) {
            flowclass.source_port = tuple.source_port;
        } else if (qstring == g_viz_constants.FlowRecordNames[FlowRecordFields::FLOWREC_DPORT]) {
            flowclass.dest_port = tuple.dest_port;
        } else if (qstring == g_viz_constants.FlowRecordNames[FlowRecordFields::FLOWREC_DIRECTION_ING]) {
            flowclass.direction = tuple.direction;
        }
    }
}

void SelectQuery::fs_write_final_result_row(const uint64_t *t, 
        const flow_tuple *tuple, const flow_stats *raw_stats,
        const flow_stats *sum_stats, const flow_stats *avg_stats,
        const std::set<boost::uuids::uuid> *flow_list) {
    AnalyticsQuery *mquery = (AnalyticsQuery *)main_query;
    bool insert_flow_class_id = false;
    bool insert_flow_count = false;
    std::map<std::string, std::string> cmap;
    boost::shared_ptr<fsMetaData> metadata;

    // first add flow tuple select fields
    for (std::vector<std::string>::const_iterator it = 
         select_column_fields.begin(); it != select_column_fields.end(); ++it) {
        std::string qstring(get_query_string(*it));
        if (qstring == g_viz_constants.FlowRecordNames[FlowRecordFields::FLOWREC_VROUTER]) {
            cmap.insert(std::make_pair(*it, tuple->vrouter));
        } else if (qstring == g_viz_constants.FlowRecordNames[FlowRecordFields::FLOWREC_SOURCEVN]) {
            cmap.insert(std::make_pair(*it, tuple->source_vn));
        } else if (qstring == g_viz_constants.FlowRecordNames[FlowRecordFields::FLOWREC_SOURCEIP]) {
            cmap.insert(std::make_pair(*it, integerToString(tuple->source_ip)));
        } else if (qstring == g_viz_constants.FlowRecordNames[FlowRecordFields::FLOWREC_DESTVN]) {
            cmap.insert(std::make_pair(*it, tuple->dest_vn));
        } else if (qstring == g_viz_constants.FlowRecordNames[FlowRecordFields::FLOWREC_DESTIP]) {
            cmap.insert(std::make_pair(*it, integerToString(tuple->dest_ip)));
        } else if (qstring == g_viz_constants.FlowRecordNames[FlowRecordFields::FLOWREC_PROTOCOL]) {
            cmap.insert(std::make_pair(*it, integerToString(tuple->protocol)));
        } else if (qstring == g_viz_constants.FlowRecordNames[FlowRecordFields::FLOWREC_SPORT]) {
            cmap.insert(std::make_pair(*it, 
                        integerToString(tuple->source_port)));
        } else if (qstring == g_viz_constants.FlowRecordNames[FlowRecordFields::FLOWREC_DPORT]) {
            cmap.insert(std::make_pair(*it, integerToString(tuple->dest_port)));
        } else if (qstring == g_viz_constants.FlowRecordNames[FlowRecordFields::FLOWREC_DIRECTION_ING]) {
            cmap.insert(std::make_pair(*it, integerToString(tuple->direction)));
        } else if (qstring == SELECT_FLOW_CLASS_ID) {
            insert_flow_class_id = true;
        } else if (qstring == SELECT_FLOW_COUNT) {
            insert_flow_count = true;
        }
    }

    if (insert_flow_class_id || 
        (fs_query_type_ == FS_SELECT_FLOW_TUPLE_STATS && 
         mquery->is_query_parallelized())) {
        size_t flow_class_id = 0;
        if (tuple) {
            flow_class_id = boost::hash_range(cmap.begin(), cmap.end());

            // look up flow_class_id in flow class id map
            std::map<size_t, flow_tuple>::iterator iter;
            iter = flow_class_id_map.find(flow_class_id);
            if (iter == flow_class_id_map.end()) {
                flow_class_id_map.insert(
                        std::make_pair(flow_class_id, *tuple));
            } else {
                if (!(iter->second == *tuple)) {
                    QE_LOG(ERROR, "Hash collision for flowclass " << 
                           iter->second << "and flow class " << *tuple);
                }
            }
        }
        // insert flow class id in result
        cmap.insert(
            std::make_pair(SELECT_FLOW_CLASS_ID,
                           integerToString(flow_class_id))); 
    }

    if (insert_flow_count) {
        size_t flow_count = 0;
        if (mquery->is_query_parallelized() && 
            ((fs_query_type_ == FS_SELECT_STATS) || 
             (fs_query_type_ == FS_SELECT_FLOW_TUPLE_STATS))) {
            assert(flow_list);
            metadata.reset(new fsMetaData(*flow_list));
        } else {
            if (flow_list) {
                flow_count = flow_list->size();
            }
        }
        cmap.insert(std::make_pair(SELECT_FLOW_COUNT, 
                    integerToString(flow_count)));
    }
  
    // done writing flow tuple information, now timeseries and stats
    if (provide_timeseries) {
        cmap.insert(std::make_pair(TIMESTAMP_FIELD, integerToString(*t)));
    }

    for (std::vector<agg_stats_t>::const_iterator it = agg_stats.begin();
         it != agg_stats.end(); ++it) {
        if (it->agg_op == RAW) {
            if (it->stat_type == PKT_STATS) {
                cmap.insert(std::make_pair(SELECT_PACKETS,
                            integerToString(raw_stats->pkts)));
            } else {
                cmap.insert(std::make_pair(SELECT_BYTES,
                            integerToString(raw_stats->bytes)));
            }
        } else if (it->agg_op == SUM) {
            if (it->stat_type == PKT_STATS) {
                cmap.insert(std::make_pair(SELECT_SUM_PACKETS, 
                            integerToString(sum_stats->pkts)));
            } else {
                cmap.insert(std::make_pair(SELECT_SUM_BYTES, 
                            integerToString(sum_stats->bytes)));
            }
        }
    }

    // Added for debugging
    if (IS_TRACE_ENABLED(POSTPROCESS_RESULT_TRACE))
    {
        std::map<std::string, std::string>::iterator tmp_it = cmap.begin();
        QE_TRACE(DEBUG, "++ Add column fields ++");
        std::vector<final_result_col> row_entry;
        for (; tmp_it != cmap.end(); tmp_it++) {
            final_result_col col;
            col.set_col(tmp_it->first); col.set_value(tmp_it->second);
            row_entry.push_back(col);
        }
        FINAL_RESULT_ROW_TRACE(QeTraceBuf, (((AnalyticsQuery *)(this->main_query))->query_id), row_entry);
    }

    // Finally, push the row into the result table 
    result_->push_back(std::make_pair(cmap, metadata));
}

inline uint64_t SelectQuery::fs_get_time_slice(const uint64_t& t) {
    AnalyticsQuery *mquery = (AnalyticsQuery*)main_query;
    uint64_t time_sample = (t - mquery->req_from_time())/granularity;
    return mquery->req_from_time() + (time_sample * granularity);
}

query_status_t SelectQuery::process_fs_query(
        process_fs_query_callback process_fs_query_cb,
        populate_fs_result_callback populate_fs_result_cb) {
    AnalyticsQuery *mquery = (AnalyticsQuery*)main_query;
    std::vector<query_result_unit_t>& where_query_result = 
        mquery->where_query_result(); 
    std::vector<query_result_unit_t>::iterator where_result_it;
    // Walk thru each entry in the where result
    for (where_result_it = where_query_result.begin(); 
         where_result_it != where_query_result.end(); ++where_result_it) {
        boost::uuids::uuid uuid;
        flow_stats stats;
        flow_tuple tuple;
        where_result_it->get_uuid_stats_8tuple(uuid, stats, tuple);
        tuple.direction = mquery->direction_ing();
        uint64_t t = where_result_it->timestamp;
        // Check if the timestamp is interesting to us
        if (t < mquery->from_time() || t > mquery->end_time()) {
            continue;
        }

        (this->*process_fs_query_cb)(t, uuid, stats, tuple);
    }

    (this->*populate_fs_result_cb)();

    return QUERY_SUCCESS;
}

void SelectQuery::process_fs_query_with_ts_stats_fields(
        const uint64_t& t, const boost::uuids::uuid& uuid, 
        const flow_stats& stats, const flow_tuple& tuple) {
    QE_TRACE(DEBUG, ""); 
    AnalyticsQuery *mquery = (AnalyticsQuery*)main_query;
    // Get the time slice
    uint64_t ts = fs_get_time_slice(t);
    if (ts > mquery->end_time()) {
        return;
    }

    // Is the time slice present in the map?
    fs_ts_stats_map_t::iterator map_it = 
        fs_ts_stats_map_.find(ts);
    if (map_it == fs_ts_stats_map_.end()) {
        map_it = (fs_ts_stats_map_.insert(std::make_pair(ts, flow_stats()))).first;
    }
    map_it->second.pkts += stats.pkts;
    map_it->second.bytes += stats.bytes;
    map_it->second.flow_list.insert(uuid);
}

void SelectQuery::populate_fs_query_result_with_ts_stats_fields() {
    QE_TRACE(DEBUG, "");
    fs_ts_stats_map_t::const_iterator map_it;
    for (map_it = fs_ts_stats_map_.begin();
         map_it != fs_ts_stats_map_.end(); ++map_it) {
        fs_write_final_result_row(&map_it->first, NULL, NULL, 
                                  &map_it->second, NULL, 
                                  &map_it->second.flow_list);
    }
}

void SelectQuery::process_fs_query_with_tuple_stats_fields(
        const uint64_t& t, const boost::uuids::uuid& uuid, 
        const flow_stats& stats, const flow_tuple& tuple) {
    QE_TRACE(DEBUG, ""); 
    // Extract the flow class
    flow_tuple flowclass;
    get_flow_class(tuple, flowclass);

    // Is there already an instance of this flow class in the map? 
    fs_tuple_stats_map_t::iterator map_it = 
        fs_tuple_stats_map_.find(flowclass);
    if (map_it == fs_tuple_stats_map_.end()) {
        map_it = (fs_tuple_stats_map_.insert(std::make_pair(
                        flowclass, flow_stats()))).first;
    }
    map_it->second.pkts += stats.pkts;
    map_it->second.bytes += stats.bytes;
    map_it->second.flow_list.insert(uuid);
}

void SelectQuery::populate_fs_query_result_with_tuple_stats_fields() {
    QE_TRACE(DEBUG, ""); 
    fs_tuple_stats_map_t::const_iterator map_it; 
    for (map_it = fs_tuple_stats_map_.begin(); 
         map_it != fs_tuple_stats_map_.end(); ++map_it) {
        fs_write_final_result_row(NULL, &map_it->first, NULL, 
                &map_it->second, NULL, &map_it->second.flow_list);
    }
}

void SelectQuery::process_fs_query_with_ts_tuple_stats_fields(
        const uint64_t& t, const boost::uuids::uuid& uuid,
        const flow_stats& stats, const flow_tuple& tuple) {
    QE_TRACE(DEBUG, ""); 
    AnalyticsQuery *mquery = (AnalyticsQuery*)main_query;
    // Extract the flow class
    flow_tuple flowclass;
    get_flow_class(tuple, flowclass);
    // Get the time slice 
    uint64_t ts = fs_get_time_slice(t);
    if (ts > mquery->end_time()) {
        QE_TRACE(DEBUG, "ts > end_time");
        return;
    }
    fs_ts_tuple_stats_map_t::iterator omap_it;
    fs_ts_stats_map_t::iterator imap_it;
    // Is there already an instance of this flow class in the map?
    omap_it = fs_ts_tuple_stats_map_.find(flowclass);
    if (omap_it == fs_ts_tuple_stats_map_.end()) {
        omap_it = (fs_ts_tuple_stats_map_.insert(std::make_pair(
                        flowclass, fs_ts_stats_map_t()))).first;
        imap_it = (omap_it->second.insert(std::make_pair(
                                        ts, flow_stats()))).first;
    } else {
        // Is the time slice present in the map?
        imap_it = omap_it->second.find(ts);
        if (imap_it == omap_it->second.end()) {
            imap_it = (omap_it->second.insert(std::make_pair(
                            ts, flow_stats()))).first;
        }
    }
    imap_it->second.pkts += stats.pkts;
    imap_it->second.bytes += stats.bytes;
    imap_it->second.flow_list.insert(uuid);
}

void SelectQuery::populate_fs_query_result_with_ts_tuple_stats_fields() {
    QE_TRACE(DEBUG, ""); 
    fs_ts_tuple_stats_map_t::const_iterator omap_it;
    fs_ts_stats_map_t::const_iterator imap_it;
    for (omap_it = fs_ts_tuple_stats_map_.begin();
         omap_it != fs_ts_tuple_stats_map_.end(); ++omap_it) {
        for (imap_it = omap_it->second.begin(); 
             imap_it != omap_it->second.end(); ++imap_it) {
            fs_write_final_result_row(&imap_it->first, &omap_it->first, NULL, 
                                      &imap_it->second, NULL, 
                                      &imap_it->second.flow_list);
        }
    }
}

void SelectQuery::process_fs_query_with_stats_fields(
        const uint64_t& t, const boost::uuids::uuid& uuid,
        const flow_stats& stats, const flow_tuple& tuple) {
    QE_TRACE(DEBUG, "");
    fs_flow_stats_.pkts += stats.pkts;
    fs_flow_stats_.bytes += stats.bytes;
    fs_flow_stats_.flow_list.insert(uuid);
}

void SelectQuery::populate_fs_query_result_with_stats_fields() {
    QE_TRACE(DEBUG, ""); 
    fs_write_final_result_row(NULL, NULL, NULL, &fs_flow_stats_, NULL,
                              &fs_flow_stats_.flow_list);
}

void SelectQuery::process_fs_query_with_ts_tuple_fields(
        const uint64_t& t, const boost::uuids::uuid& uuid,
        const flow_stats& stats, const flow_tuple& tuple) {
    QE_TRACE(DEBUG, ""); 
    AnalyticsQuery *mquery = (AnalyticsQuery*)main_query;
    // Extract the flow class
    flow_tuple flowclass;
    get_flow_class(tuple, flowclass);
    // Get the time slice
    uint64_t ts = fs_get_time_slice(t); 
    if (ts > mquery->end_time()) {
        return;
    }
    // Is the time slice present in the map?
    fs_ts_tuple_map_t::iterator map_it = fs_ts_tuple_map_.find(ts);
    if (map_it == fs_ts_tuple_map_.end()) {
        map_it = (fs_ts_tuple_map_.insert(
                    std::make_pair(ts, std::set<flow_tuple>()))).first;
        map_it->second.insert(flowclass);
    } else {
        std::set<flow_tuple>::iterator it = map_it->second.find(tuple);
        if (it == map_it->second.end()) {
            map_it->second.insert(flowclass);
        }
    }
}

void SelectQuery::populate_fs_query_result_with_ts_tuple_fields() {
    QE_TRACE(DEBUG, ""); 
    fs_ts_tuple_map_t::const_iterator map_it;
    for (map_it = fs_ts_tuple_map_.begin();
         map_it != fs_ts_tuple_map_.end(); ++map_it) {
        std::set<flow_tuple>::const_iterator it;
        for (it = map_it->second.begin(); it != map_it->second.end(); ++it) {
            fs_write_final_result_row(&map_it->first, &(*it), NULL, NULL, NULL);
        }
    }
}

void SelectQuery::process_fs_query_with_ts(const uint64_t& t,
        const boost::uuids::uuid& uuid, const flow_stats& stats,
        const flow_tuple& tuple) {
    QE_TRACE(DEBUG, "");
    fs_ts_list_.insert(fs_get_time_slice(t));
}

void SelectQuery::populate_fs_query_result_with_ts() {
    QE_TRACE(DEBUG, "");
    for (fs_ts_t::iterator it = fs_ts_list_.begin(); 
         it != fs_ts_list_.end(); ++it) {
        fs_write_final_result_row(&(*it), NULL, NULL, NULL, NULL);
    }
}

void SelectQuery::process_fs_query_with_time_tuple_stats_fields(
        const uint64_t& t, const boost::uuids::uuid& uuid,
        const flow_stats& stats, const flow_tuple& tuple) {
    QE_TRACE(DEBUG, "");
    fs_write_final_result_row(&t, &tuple, &stats, NULL, NULL); 
}

void SelectQuery::populate_fs_query_result_with_time_tuple_stats_fields() {
    QE_TRACE(DEBUG, "");
}
