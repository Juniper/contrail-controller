/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

/*
 * This file has the interface for handling StatsOracle select processing
 * 
 */

#ifndef STATS_SELECT_H_
#define STATS_SELECT_H_

#include <vector>
#include <string>
#include <map>
#include <set>
#include <utility>
#include <boost/variant.hpp>
#include <boost/uuid/uuid.hpp>
#include "QEOpServerProxy.h"
#include "query.h"

class AnalyticsQuery;

class StatsSelect {
public:
    typedef QEOpServerProxy::SubVal StatVal;
    typedef QEOpServerProxy::VarType StatType;
    typedef QEOpServerProxy::AggOper StatOper;
    typedef QEOpServerProxy::OutRowMultimapT MapBufT;
    typedef std::map<std::pair<QEOpServerProxy::AggOper,std::string>, size_t> AggSortT;

    typedef std::map<std::string, StatVal> StatMap;
    struct StatEntry {
        std::string name;
        StatVal value;
    };

    StatsSelect(AnalyticsQuery * main_query, const std::vector<std::string> & select_fields);

    // This should be called after the post processing is over
    void SetSortOrder(const std::vector<sort_field_t>& sort_fields);

    // The client call this function once with every row from the where result.
    // cols that are not in the SELECT will be silently dropped.
    bool LoadRow(boost::uuids::uuid u, uint64_t timestamp,
            const std::vector<StatEntry>& row, MapBufT& output);

    bool Status() { return status_; }

    bool IsMergeNeeded() { return !isT_; }

    static void Merge(const MapBufT& input, MapBufT& output);
    void MergeFinal(const std::vector<boost::shared_ptr<MapBufT> >& inputs,
        MapBufT& output);

    static QEOpServerProxy::AggOper ParseAgg(
            const std::string& vname,
            std::string& sfield);

    static bool Jsonify(const std::map<std::string, StatVal>&, 
            const QEOpServerProxy::AggRowT&, std::string& jstr);

private:

    static void MergeAggRow(QEOpServerProxy::AggRowT &arows,
            const QEOpServerProxy::AggRowT &narows);
    static void MergeFullRow(
            const std::vector<StatVal>& ukey,
            const StatMap& uniks,
            const QEOpServerProxy::AggRowT& narows,
            MapBufT& output);

    bool isStatic_;
    bool status_;

    AnalyticsQuery * const main_query;
    const std::vector<std::string> select_fields_;

    // If T= is in the SELECT, this gives the timeperiod
    uint32_t ts_period_;
    bool isT_;

    // Is CLASS(T) or CLASS(T=) in the SELECT Clause 
    bool isTC_;
    bool isTBC_;

    // This is the column name corresponding to the COUNT select field.
    // It will be empty if the SELECT did not have COUNT.
    std::string count_field_;

    // This is the set of columns names to be used for sorting
    // The value is a sequence number - this is the position to use for this column
    // in the sort vector
    std::map<std::string, size_t> sort_cols_;
    AggSortT agg_sort_cols_;

    // This is the set of columns that define uniqueness of rows
    // They will correspond to the unique-map used to generate the row hash
    std::set<std::string> unik_cols_;

    // This is the set of columns that require aggregation.
    std::set<std::string> sum_cols_;
    std::set<std::string> class_cols_;

};
#endif
