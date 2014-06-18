/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/assign/list_of.hpp>
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"
#include "query.h"

using boost::assign::map_list_of;

// compare flow records based on UUID
bool PostProcessingQuery::flow_record_comparator(
                            const QEOpServerProxy::ResultRowT& lhs,
                            const QEOpServerProxy::ResultRowT& rhs) {
    std::map<std::string, std::string>::const_iterator lhs_it, rhs_it;
    lhs_it = lhs.first.find(g_viz_constants.UUID_KEY);
    QE_ASSERT(lhs_it != lhs.first.end());
    rhs_it = rhs.first.find(g_viz_constants.UUID_KEY);
    QE_ASSERT(rhs_it != rhs.first.end());
    if (lhs_it->second < rhs_it->second) return true;
    if (lhs_it->second > rhs_it->second) return false;

    return false;
}

bool PostProcessingQuery::sort_field_comparator(
        const QEOpServerProxy::ResultRowT& lhs,
        const QEOpServerProxy::ResultRowT& rhs) {
    std::map<std::string, std::string>::const_iterator lhs_it, rhs_it;
    for (std::vector<sort_field_t>::iterator sort_it = sort_fields.begin();
         sort_it != sort_fields.end(); sort_it++) {
        lhs_it = lhs.first.find((*sort_it).name);
        QE_ASSERT(lhs_it != lhs.first.end());
        rhs_it = rhs.first.find((*sort_it).name);
        QE_ASSERT(rhs_it != rhs.first.end());
        if ((*sort_it).type == std::string("int") || 
            (*sort_it).type == std::string("long") ||
            (*sort_it).type == std::string("ipv4")) {
            uint64_t lhs_val = 0, rhs_val = 0;
            stringToInteger(lhs_it->second, lhs_val);
            stringToInteger(rhs_it->second, rhs_val);
            if (lhs_val < rhs_val) return true;
            if (lhs_val > rhs_val) return false;
        } else {
            if (lhs_it->second < rhs_it->second) return true;
            if (lhs_it->second > rhs_it->second) return false;
        }
    }

    return false;
}

bool PostProcessingQuery::flowseries_merge_processing(
        const QEOpServerProxy::BufferT *raw_result,
        QEOpServerProxy::BufferT* merged_result, 
        fcid_rrow_map_t *fcid_rrow_map) {
    AnalyticsQuery *mquery = (AnalyticsQuery *)main_query;

    switch(mquery->selectquery_->flowseries_query_type()) {
    case SelectQuery::FS_SELECT_STATS:
        fs_stats_merge_processing(raw_result, merged_result);
        break;
    case SelectQuery::FS_SELECT_FLOW_TUPLE_STATS:
        fs_tuple_stats_merge_processing(raw_result, merged_result, fcid_rrow_map);
        break;
    default:
        return false;
    }

    return true;
}

void PostProcessingQuery::fs_merge_stats(
            const QEOpServerProxy::ResultRowT& input,
            QEOpServerProxy::ResultRowT& output) {
    QEOpServerProxy::OutRowT::iterator pit = 
            output.first.find(SELECT_SUM_PACKETS);
    if (pit != output.first.end()) {
        uint64_t sum_pkts = 0;
        stringToInteger(pit->second, sum_pkts);
        uint64_t pkts = 0;
        stringToInteger(input.first.find(SELECT_SUM_PACKETS)->second, pkts);
        sum_pkts += pkts;
        pit->second = integerToString(sum_pkts);
    }
    QEOpServerProxy::OutRowT::iterator bit = 
            output.first.find(SELECT_SUM_BYTES);
    if (bit != output.first.end()) {
        uint64_t sum_bytes = 0;
        stringToInteger(bit->second, sum_bytes);
        uint64_t bytes = 0;
        stringToInteger(input.first.find(SELECT_SUM_BYTES)->second, bytes);
        sum_bytes += bytes;
        bit->second = integerToString(sum_bytes);
    }
    if (input.second.get()) {
        fsMetaData *imetadata = static_cast<fsMetaData*>(input.second.get());
        fsMetaData *ometadata = static_cast<fsMetaData*>(output.second.get());
        ometadata->uuids.insert(imetadata->uuids.begin(), 
                                imetadata->uuids.end());
    }
}

void PostProcessingQuery::fs_stats_merge_processing(
        const QEOpServerProxy::BufferT *raw_result, 
        QEOpServerProxy::BufferT *merged_result) {
    if (!raw_result->size()) {
        return;
    }
    if (!merged_result->size()) {
        merged_result->reserve(raw_result->size());
        copy(raw_result->begin(), raw_result->end(),
             std::back_inserter(*merged_result));
        return;
    }
    assert(raw_result->size() == 1);
    assert(merged_result->size() == 1);
    QEOpServerProxy::ResultRowT& mresult_row = merged_result->at(0);
    const QEOpServerProxy::ResultRowT& rresult_row = raw_result->at(0);
    QE_TRACE(DEBUG, "fs_stats_merge_processing: merge_stats.");
    fs_merge_stats(rresult_row, mresult_row);
}

void PostProcessingQuery::fs_tuple_stats_merge_processing(
        const QEOpServerProxy::BufferT *raw_result, 
        QEOpServerProxy::BufferT *merged_result,
        fcid_rrow_map_t *fcid_rrow_map) {

    for (size_t r = 0; r < merged_result->size(); ++r) {
        const QEOpServerProxy::ResultRowT& rresult_row = merged_result->at(r);
        const QEOpServerProxy::OutRowT& ro_row = rresult_row.first;
        QEOpServerProxy::OutRowT::const_iterator rfc_it = 
            rresult_row.first.find(SELECT_FLOW_CLASS_ID);
        assert(rfc_it != ro_row.end());
        uint64_t rfc_id = 0;
        stringToInteger(rfc_it->second, rfc_id);
        if (fcid_rrow_map->find(rfc_id) == fcid_rrow_map->end()) {
            // flow_class_id rfc_id not found, add the result row to the map
            fcid_rrow_map->insert(std::pair<uint64_t, 
                QEOpServerProxy::ResultRowT>(rfc_id, rresult_row));
        } else {
            // found an existing flow class id in the map
            QEOpServerProxy::ResultRowT& mresult_row = 
                (fcid_rrow_map->find(rfc_id))->second;
            fs_merge_stats(rresult_row, mresult_row);
        }
    }
    for (size_t r = 0; r < raw_result->size(); ++r) {
        const QEOpServerProxy::ResultRowT& rresult_row = raw_result->at(r);
        const QEOpServerProxy::OutRowT& ro_row = rresult_row.first;
        QEOpServerProxy::OutRowT::const_iterator rfc_it = 
            rresult_row.first.find(SELECT_FLOW_CLASS_ID);
        assert(rfc_it != ro_row.end());
        uint64_t rfc_id = 0;
        stringToInteger(rfc_it->second, rfc_id);
        if (fcid_rrow_map->find(rfc_id) == fcid_rrow_map->end()) {
            // flow_class_id rfc_id not found, add the result row to the map
            fcid_rrow_map->insert(std::pair<uint64_t, 
                QEOpServerProxy::ResultRowT>(rfc_id, rresult_row));
        } else {
            // found an existing flow class id in the map
            QEOpServerProxy::ResultRowT& mresult_row = 
                (fcid_rrow_map->find(rfc_id))->second;
            fs_merge_stats(rresult_row, mresult_row);
        }
    }
}

void PostProcessingQuery::fs_update_flow_count(
            QEOpServerProxy::ResultRowT& rrow) {
    QEOpServerProxy::OutRowT::iterator fit = 
        rrow.first.find(SELECT_FLOW_COUNT);
    assert(fit != rrow.first.end());
    assert(rrow.second.get());
    fsMetaData *mdata = 
        static_cast<fsMetaData*>(rrow.second.get());
    fit->second = integerToString(mdata->uuids.size());
}

bool PostProcessingQuery::merge_processing(
        const QEOpServerProxy::BufferT& input, 
        QEOpServerProxy::BufferT& output)
{
    AnalyticsQuery *mquery = (AnalyticsQuery *)main_query;

    if (status_details != 0)
    {
        QE_TRACE(DEBUG, 
             "No need to process query, as there were errors previously");
        return false;
    }


    if (mquery->table() == g_viz_constants.FLOW_SERIES_TABLE) {
        fcid_rrow_map_t fcid_rrow_map;
        if (flowseries_merge_processing(&input, &output, &fcid_rrow_map)) {
            if (fcid_rrow_map.size() != 0) {
                output.clear();
                for(fcid_rrow_map_t::iterator it = fcid_rrow_map.begin(); it != fcid_rrow_map.end(); ++it ) {
                    output.push_back(it->second);
                }
            }
            status_details = 0;
            return true;
        }
    }

    // Check if the result has to be sorted
    if (sorted) {
        QEOpServerProxy::BufferT *merged_result = &output;
        const QEOpServerProxy::BufferT *raw_result1 = &(input);

        if (result_.get() == NULL) {
            size_t merged_result_size = merged_result->size();
            merged_result->reserve(merged_result_size + raw_result1->size());
            copy(raw_result1->begin(), raw_result1->end(), 
                 std::back_inserter(*merged_result));
            if (merged_result_size) { 
                if (sorting_type == ASCENDING) {
                    std::inplace_merge(merged_result->begin(), 
                        merged_result->begin() + merged_result_size,
                        merged_result->end(),
                        boost::bind(&PostProcessingQuery::sort_field_comparator, 
                                    this, _1, _2));
                } else {
                    std::inplace_merge(merged_result->rbegin(),
                        merged_result->rbegin() + raw_result1->size(),
                        merged_result->rend(),
                        boost::bind(&PostProcessingQuery::sort_field_comparator,
                                    this, _1, _2));
                }
            }

            goto sort_done;
        }

        QEOpServerProxy::BufferT *raw_result2 = result_.get();
        size_t size1 = raw_result1->size();
        size_t size2 = raw_result2->size();
        QE_TRACE(DEBUG, "Merging results from vectors of size:" <<
                size1 << " and " << size2);
        merged_result->reserve(raw_result1->size() + raw_result2->size());
        if (sorting_type == ASCENDING) {
            std::merge(raw_result1->begin(), raw_result1->end(), 
                raw_result2->begin(), raw_result2->end(),
                std::back_inserter(*merged_result),
                boost::bind(&PostProcessingQuery::sort_field_comparator, 
                            this, _1, _2)); 
        } else {
            std::merge(raw_result1->rbegin(), raw_result1->rend(), 
                raw_result2->rbegin(), raw_result2->rend(),
                std::back_inserter(*merged_result),
                boost::bind(&PostProcessingQuery::sort_field_comparator, 
                            this, _1, _2)); 
        }
    } 

sort_done:
    if (!sorted && (mquery->table() == g_viz_constants.FLOW_TABLE))
    {
        QE_TRACE(DEBUG, "Merge_Processing: Adding inputs to output");
        QEOpServerProxy::BufferT *merged_result = &output;
        const QEOpServerProxy::BufferT *raw_result1 = &(input);

        if (result_.get() == NULL)
        {
            merged_result->reserve(raw_result1->size());
            copy(raw_result1->begin(), raw_result1->end(), 
                std::back_inserter(*merged_result));
        } else {

            QEOpServerProxy::BufferT *raw_result2 = result_.get();
            size_t size1 = raw_result1->size();
            size_t size2 = raw_result2->size();
            QE_TRACE(DEBUG, "Merging results from vectors of size:" <<
                    size1 << " and " << size2);
            merged_result->reserve(raw_result1->size() + raw_result2->size());
            copy(raw_result1->begin(), raw_result1->end(), 
                std::back_inserter(*merged_result));
            copy(raw_result2->begin(), raw_result2->end(), 
                std::back_inserter(*merged_result));
        }
        QE_TRACE(DEBUG, "Merge_Processing: Done adding inputs to output");
    }

    // Have the result ready and processing is done
    status_details = 0;
    return true;
}

bool PostProcessingQuery::final_merge_processing(
const std::vector<boost::shared_ptr<QEOpServerProxy::BufferT> >& inputs,
                        QEOpServerProxy::BufferT& output)
{
    AnalyticsQuery *mquery = (AnalyticsQuery *)main_query;

    if (status_details != 0)
    {
        QE_TRACE(DEBUG, 
             "No need to process query, as there were errors previously");
        return false;
    }

    if (mquery->table() == g_viz_constants.FLOW_SERIES_TABLE) {
        fcid_rrow_map_t fcid_rrow_map;
        bool status = false;
        for (size_t i = 0; i < inputs.size(); i++) {
            status = flowseries_merge_processing(inputs[i].get(), 
                                    &output, &fcid_rrow_map);
        }
        if (status) {
            bool is_select_flow_count = 
                mquery->selectquery_->is_present_in_select_column_fields(
                                                        SELECT_FLOW_COUNT);
            if (fcid_rrow_map.size() != 0) {
                for(fcid_rrow_map_t::iterator it = fcid_rrow_map.begin(); 
                    it != fcid_rrow_map.end(); ++it ) {
                    if (is_select_flow_count) {
                        fs_update_flow_count(it->second);
                    }
                    output.push_back(it->second);
                }
            }

            if (mquery->selectquery_->flowseries_query_type() == 
                SelectQuery::FS_SELECT_STATS && is_select_flow_count) {
                if (output.size()) {
                    fs_update_flow_count(output[0]);
                }
            }

            if (sorted) {
                QEOpServerProxy::BufferT *merged_result = &output;
                if (sorting_type == ASCENDING) {
                    std::sort(merged_result->begin(), merged_result->end(), 
                              boost::bind(
                                &PostProcessingQuery::sort_field_comparator, 
                                          this, _1, _2)); 
                } else {
                    std::sort(merged_result->rbegin(), merged_result->rend(), 
                              boost::bind(
                                &PostProcessingQuery::sort_field_comparator,
                                          this, _1, _2));
                }
            }
            goto limit;
        }
    }

    if (mquery->table() == g_viz_constants.FLOW_TABLE)
    {
        QE_TRACE(DEBUG, "Final_Merge_Processing: Uniquify flow records");
        // uniquify the records
        std::set<QEOpServerProxy::ResultRowT, 
                 bool(*)(const QEOpServerProxy::ResultRowT&,
                         const QEOpServerProxy::ResultRowT&)>
                 result_row_set(&PostProcessingQuery::flow_record_comparator);

        for (size_t i = 0; i < inputs.size(); i++)
        {
            QEOpServerProxy::BufferT *raw_result = inputs[i].get();
            result_row_set.insert(raw_result->begin(), raw_result->end());
        }

        QEOpServerProxy::BufferT *merged_result = &output;
        merged_result->reserve(result_row_set.size());
        copy(result_row_set.begin(), result_row_set.end(), 
            std::back_inserter(*merged_result));

        QE_TRACE(DEBUG, "Final_Merge_Processing: Done uniquify flow records");
        // Check if the result has to be sorted
        if (sorted) {
            if (sorting_type == ASCENDING) {
                std::sort(merged_result->begin(), merged_result->end(), 
                          boost::bind(&PostProcessingQuery::sort_field_comparator, 
                                      this, _1, _2)); 
            } else {
                std::sort(merged_result->rbegin(), merged_result->rend(), 
                          boost::bind(&PostProcessingQuery::sort_field_comparator,
                                      this, _1, _2));
            }
        }
    } else {  // For non-flow-record queries
        // Check if the result has to be sorted
        if (sorted) {
            QEOpServerProxy::BufferT *merged_result = &output;
            size_t final_vector_size = 0;
            for (size_t i = 0; i < inputs.size(); i++)
                final_vector_size += inputs[i]->size();
        
            QE_TRACE(DEBUG, "Merging results between " << inputs.size() 
                    << " vectors with final vector size:" << final_vector_size);

            merged_result->reserve(final_vector_size);

            for (size_t i = 0; i < inputs.size(); i++)
            {
                QEOpServerProxy::BufferT *raw_result = inputs[i].get();
                if (sorting_type == ASCENDING) {
                    QEOpServerProxy::BufferT::iterator
                        current_result_end = merged_result->end();
                    copy(raw_result->begin(), raw_result->end(), 
                        std::back_inserter(*merged_result));
                    std::inplace_merge(merged_result->begin(),
                    current_result_end, merged_result->end(),
    boost::bind(&PostProcessingQuery::sort_field_comparator, this, _1, _2)); 
                } else {
                    QEOpServerProxy::BufferT::reverse_iterator
                        current_result_end = merged_result->rbegin();
                    copy(raw_result->begin(), raw_result->end(), 
                        std::back_inserter(*merged_result));
                    std::inplace_merge(merged_result->rbegin(),
                    current_result_end, merged_result->rend(),
    boost::bind(&PostProcessingQuery::sort_field_comparator, this, _1, _2)); 
                }
            }
        }
    }
   
limit:
    if (limit) {
        QEOpServerProxy::BufferT *merged_result = &output;
        QE_TRACE(DEBUG, "Apply Limit [" << limit << "]");
        if (merged_result->size() > (size_t)limit) {
            merged_result->resize(limit);
        }
    }

    // Have the result ready and processing is done
    status_details = 0;
    return true;
}

query_status_t PostProcessingQuery::process_query() {
    if (status_details != 0)
    {
        QE_TRACE(DEBUG, 
             "No need to process query, as there were errors previously");
        return QUERY_FAILURE;
    }

    AnalyticsQuery *mquery = (AnalyticsQuery *)main_query;
    result_ = mquery->selectquery_->result_;
    mresult_ = mquery->selectquery_->mresult_;
    QEOpServerProxy::BufferT *raw_result = result_.get();

    /* filter are ANDs over OR
     * [ [ e1 AND e2 ] OR [ e3 ] ]
     */
    /* below is filter processing for stats table queries
     */
    if (filter_list.size()) {
        size_t num_filtered=0;
        MapBufT::iterator kt = mresult_->end();
        for (MapBufT::iterator it = mresult_->begin();
                it!= mresult_->end(); it++) {

            if (kt!=mresult_->end()) {
                mresult_->erase(kt);
                kt = mresult_->end();
            }
            std::map<std::string, QEOpServerProxy::SubVal>& attrs = it->second.first;
            bool delete_row = true;
            std::string unknown_attr; 
            for (size_t j = 0; j < filter_list.size(); j++) {
                std::vector<filter_match_t>& filter_and = filter_list[j];
                bool and_check = true;

                for (size_t k = 0; k < filter_and.size(); k++) {
                    std::map<std::string, QEOpServerProxy::SubVal>::const_iterator iter =
                        attrs.find(filter_and[k].name);
                    if (iter == attrs.end()) {
                        unknown_attr = filter_and[k].name;
                        break;
                    } else {
                        unknown_attr.clear();
                    }
                    std::ostringstream vstream;
                    vstream << iter->second;

                    switch(filter_and[k].op) {
                        case EQUAL:
                            if (filter_and[k].value != vstream.str())
                              {
                                and_check = false;
                              }
                            break;
                        case  NOT_EQUAL:
                            if (filter_and[k].value == vstream.str())
                              {
                                and_check = false;
                              }
                            break;
                        default:
                            // upsupported filter operation
                            QE_TRACE(DEBUG, "Unsupported filter operation " << filter_and[k].op);
                            break;
                    }
                    if (and_check == false)
                        break;
                }
                if (and_check == true) {
                    delete_row = false;
                    break;
                }
            }
            if (unknown_attr.size()) {
                QE_TRACE(DEBUG, "Unknown filter attr in row " << unknown_attr);
            }
            if (delete_row) {
                num_filtered++;
                kt = it;
            } 
        }
        if (kt!=mresult_->end()) {
            mresult_->erase(kt);
            kt = mresult_->end();
        }
        QE_TRACE(DEBUG, "# of entries filtered is " << num_filtered);

    }

    /* below is filter processing for non stats table queries
     */
    if (filter_list.size() != 0) {
        QEOpServerProxy::BufferT filtered_table;
        // do filter operation
        QE_TRACE(DEBUG, "Doing filter operation");
        for (size_t i = 0; i < raw_result->size(); i++) {
            QEOpServerProxy::ResultRowT row = (*raw_result)[i];
            bool delete_row = true;

            for (size_t j = 0; j < filter_list.size(); j++) {
                std::vector<filter_match_t>& filter_and = filter_list[j];
                bool and_check = true;

                for (size_t k = 0; k < filter_and.size(); k++) {
                    std::map<std::string, std::string>::iterator iter;
                    iter = row.first.find(filter_and[k].name);
                    if (iter == row.first.end())
                      {
                        if (!(filter_and[k].ignore_col_absence)) {
                            and_check = false;
                            break;
                        } 
                        continue;
                      }

                    switch(filter_and[k].op)
                      {
                        case EQUAL:
                            if (filter_and[k].value != iter->second)
                              {
                                and_check = false;
                              }
                            break;

                        case NOT_EQUAL:
                            if (filter_and[k].value == iter->second)
                              {
                                and_check = false;
                              }
                            break;

                        case LEQ:
                              {
                                int filter_value = 
                                    atoi(filter_and[k].value.c_str());
                                int column_value= atoi(iter->second.c_str());
                                if (column_value > filter_value)
                                  {
                                    and_check = false;
                                  }
                                break;
                              }

                        case GEQ:
                              {
                                int filter_value = 
                                    atoi(filter_and[k].value.c_str());
                                int column_value= atoi(iter->second.c_str());
                                if (column_value < filter_value)
                                  {
                                    and_check = false;
                                  }
                                break;
                              }

                        case REGEX_MATCH:
                              {
                                if (!boost::regex_match(iter->second, 
                                            filter_and[k].match_e))
                                  {
                                    and_check = false;
                                  }
                                break;
                              }

                        default:
                            // upsupported filter operation
                            QE_LOG(ERROR, "Unsupported filter operation: " <<
                                    filter_and[k].op);
                            return QUERY_FAILURE;
                      }
                    if (and_check == false)
                        break;
                }

                if (and_check == true) {
                    QE_TRACE(DEBUG, "filter out entry #:" << i);
                    delete_row = false;
                    break;
                }
            }
            if (!delete_row) {
                filtered_table.push_back(row);
            }
        }
        *raw_result = filtered_table;
    }

    // Check if the result has to be sorted
    if (sorted) {
        if (sorting_type == ASCENDING) {
            std::sort(raw_result->begin(), raw_result->end(), 
                      boost::bind(&PostProcessingQuery::sort_field_comparator, 
                                  this, _1, _2)); 
        } else {
            std::sort(raw_result->rbegin(), raw_result->rend(), 
                      boost::bind(&PostProcessingQuery::sort_field_comparator,
                                  this, _1, _2));
        }
    }

    // If the flow series query is parallelized, we should apply the limit 
    // only after the result from all the tasks are merged 
    // (@ final_merge_processing).
    if ((mquery->table() != g_viz_constants.FLOW_SERIES_TABLE || 
        (mquery->table() == g_viz_constants.FLOW_SERIES_TABLE && 
        !mquery->is_query_parallelized())) && limit) {
        QE_TRACE(DEBUG, "Apply Limit [" << limit << "]");
        if (raw_result->size() > (size_t)limit) {
            raw_result->resize(limit);
        }
    }

    if (IS_TRACE_ENABLED(POSTPROCESS_RESULT_TRACE))
    {
        std::vector<QEOpServerProxy::ResultRowT>::iterator res_it;
        QE_TRACE(DEBUG, "== Post Processing Result ==");
        for (res_it = raw_result->begin(); res_it != raw_result->end(); 
             ++res_it) {
            std::vector<final_result_col> row_entry;
            std::map<std::string, std::string>::iterator map_it;
            for (map_it = (*res_it).first.begin(); 
                 map_it != (*res_it).first.end(); ++map_it) {
                final_result_col col;
                col.set_col(map_it->first); col.set_value(map_it->second);
                row_entry.push_back(col);
                //QE_TRACE(DEBUG, map_it->first << " : " << map_it->second);
            }
            FINAL_RESULT_ROW_TRACE(QeTraceBuf, mquery->query_id, row_entry);
        }
    }

#if 0 
    //if ((limit) && (!sorted))
    for (int i = 0 ; i < 200000; i++)
    raw_result->push_back(map_list_of(
            "destvn","abc-cor\"poration:front-end-network:001")(
            "sourceip","168430090")("destip","3232238090")(
            "sourcevn","abc-corporation:front-end-network:002")(
            "protocol","80")("dport","62000")("sport","1000")(
            "sum(packets)","4294967196")
        );
#endif

    // Have the result ready and processing is done
    status_details = 0;
    parent_query->subquery_processed(this);
    return QUERY_SUCCESS;
}
