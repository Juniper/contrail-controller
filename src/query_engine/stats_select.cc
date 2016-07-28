/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */
#include "viz_constants.h"
#include "stats_select.h"
#include "stats_query.h"
#include "query.h"
#include <cstdlib>
#include <boost/assign/list_of.hpp>
#include <boost/functional/hash.hpp>
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

using boost::assign::map_list_of; 

using std::string;
using std::map;
using std::vector;
using std::set;
using std::pair;
using std::make_pair;

struct Centroid {
};

void
StatsSelect::DeleteCentroid (Centroid *t) {
    free(t);
}

struct TDigest {
};

void 
StatsSelect::DeleteTDigest(TDigest *t) {
    TDigest_destroy(t);
}

bool
StatsSelect::Jsonify(const std::map<std::string, StatVal>&  uniks, 
        const QEOpServerProxy::AggRowT& aggs, std::string& jstr) {

    rapidjson::Document dd;
    dd.SetObject();  

    try {
        for (std::map<std::string, StatVal>::const_iterator it = uniks.begin();
                it!= uniks.end(); it++) {
            switch (it->second.which()) {
                case QEOpServerProxy::STRING : {
                        string mapit = boost::get<string>(it->second);
                        rapidjson::Value val(rapidjson::kStringType);
                        val.SetString(mapit.c_str());
                        dd.AddMember(it->first.c_str(), val, dd.GetAllocator());              
                    }
                    break;
                case QEOpServerProxy::UUID : {
                        boost::uuids::uuid mapit = boost::get<boost::uuids::uuid>(it->second);
                        rapidjson::Value val(rapidjson::kStringType);
                        std::string ustr = to_string(mapit);
                        val.SetString(ustr.c_str(), dd.GetAllocator());
                        dd.AddMember(it->first.c_str(), val, dd.GetAllocator()); 
                    }
                    break;
                case QEOpServerProxy::UINT64 : {
                        uint64_t mapit = boost::get<uint64_t>(it->second);
                        rapidjson::Value val(rapidjson::kNumberType);
                        val.SetUint64(mapit);
                        dd.AddMember(it->first.c_str(), val, dd.GetAllocator());
                    }
                    break;
                case QEOpServerProxy::DOUBLE : {
                        double mapit = boost::get<double>(it->second);
                        rapidjson::Value val(rapidjson::kNumberType);
                        val.SetDouble(mapit);
                        dd.AddMember(it->first.c_str(), val, dd.GetAllocator());
                    }
                    break;
                default:
                    QE_ASSERT(0);
            }
        }
        for (QEOpServerProxy::AggRowT::const_iterator it = aggs.begin();
                it!= aggs.end(); it++) {
            
            string sname("");
            if (it->first.first == QEOpServerProxy::SUM) {
                sname = string("SUM(") + it->first.second + string(")");
            } else if (it->first.first == QEOpServerProxy::COUNT) {
                sname = string("COUNT(") + it->first.second + string(")");
            } else if (it->first.first == QEOpServerProxy::CLASS) {
                sname = string("CLASS(") + it->first.second + string(")");
            } else if (it->first.first == QEOpServerProxy::MAX) {
                sname = string("MAX(") + it->first.second + string(")");
            } else if (it->first.first == QEOpServerProxy::MIN) {
                sname = string("MIN(") + it->first.second + string(")");
            } else if (it->first.first == QEOpServerProxy::PERCENTILES) {
                sname = string("PERCENTILES(") + it->first.second + string(")");
            } else if (it->first.first == QEOpServerProxy::AVG) {
                sname = string("AVG(") + it->first.second + string(")");
            } else {
                QE_ASSERT(0);
            }
            switch (it->second.which()) {

                case QEOpServerProxy::UINT64 : {
                        uint64_t mapit = boost::get<uint64_t>(it->second);
                        rapidjson::Value val(rapidjson::kNumberType);
                        val.SetUint64(mapit);
                        dd.AddMember(sname.c_str(), dd.GetAllocator(),
                            val, dd.GetAllocator());
                    }
                    break;
                case QEOpServerProxy::DOUBLE : {
                        double mapit = boost::get<double>(it->second);
                        rapidjson::Value val(rapidjson::kNumberType);
                        val.SetDouble(mapit);
                        dd.AddMember(sname.c_str(), dd.GetAllocator(),
                            val, dd.GetAllocator());
                    }
                    break;
                case QEOpServerProxy::TDIGEST : {
                        boost::shared_ptr<TDigest> mapit =
                            boost::get<boost::shared_ptr<TDigest> >(it->second);
                        
                        rapidjson::Value val(rapidjson::kObjectType);
                        float ptiles[7] = {.01,.05,.25,.5,.75,.95,.99};
                        std::string stiles[7] = {"01","05","25","50","75","95","99"};
#if 0
                        size_t jj = TDigest_get_ncentroids(mapit.get());
                        for (size_t kk=0; kk < jj; kk++) {
                            Centroid *c = TDigest_get_centroid(mapit.get(), kk);
                            std::cout << "Parse Centroid " <<
                               Centroid_get_mean(c) << " " <<
                               Centroid_get_count(c) << " " <<
                               Centroid_quantile(c, mapit.get()) << std::endl;
                        }
#endif
                        for (size_t idx=0; idx<7; idx++) {
                            rapidjson::Value sval(rapidjson::kNumberType);
                            sval.SetDouble(TDigest_percentile(mapit.get(), ptiles[idx]));
                            val.AddMember(stiles[idx].c_str(), dd.GetAllocator(),
                                sval, dd.GetAllocator());
                        }
                        dd.AddMember(sname.c_str(), dd.GetAllocator(),
                            val, dd.GetAllocator());
                    }
                    break;
                case QEOpServerProxy::CENTROID : {
                        boost::shared_ptr<Centroid> mapit =
                            boost::get<boost::shared_ptr<Centroid> >(it->second);
                        rapidjson::Value val(rapidjson::kNumberType);
                        val.SetDouble(Centroid_get_mean(mapit.get()));
                        dd.AddMember(sname.c_str(), dd.GetAllocator(),
                            val, dd.GetAllocator());
                    }
                    break;
                default:
                    QE_ASSERT(0);
            }
        }
    } catch (boost::bad_get& ex) {
        QE_ASSERT(0);
    } catch (const std::out_of_range& oor) {
        QE_ASSERT(0);
    }
    rapidjson::StringBuffer sb;
    rapidjson::Writer<rapidjson::StringBuffer> writer(sb);
    dd.Accept(writer);
    jstr = sb.GetString();
    //QE_LOG_NOQID(INFO, "Jsonify row " << jstr);
    return true;
}


void StatsSelect::MergeFinal(const std::vector<boost::shared_ptr<MapBufT> >& inputs,
        MapBufT& output) {
    MapBufT temp;
    for (size_t idx = 0; idx < inputs.size(); idx++) {
        if (agg_sort_cols_.empty())
            Merge(*inputs[idx], output);
        else
            Merge(*inputs[idx], temp); 
    }
    if (!agg_sort_cols_.empty()) {
        MapBufT::iterator jt = temp.end();
        for (MapBufT::iterator it = temp.begin(); it!= temp.end(); it++) {
            if (jt!=temp.end()) {
                temp.erase(jt);
            }
            std::vector<StatVal> ukey = it->first;
            const StatMap& uniks = it->second.first;
            const QEOpServerProxy::AggRowT& narows = it->second.second;
            for (AggSortT::const_iterator xt = agg_sort_cols_.begin();
                    xt != agg_sort_cols_.end(); xt++) {
                QE_ASSERT(narows.find(xt->first) != narows.end());
                ukey[xt->second] = narows.at(xt->first);
            }
            MergeFullRow(ukey, uniks, narows, output);
            jt = it;
        }
        temp.clear();
    }
}

void StatsSelect::Merge(const MapBufT& input, MapBufT& output) {
    for (MapBufT::const_iterator it = input.begin(); 
            it!=input.end(); it++) {
        const std::vector<StatVal>& ukey = it->first;
        const StatMap& uniks = it->second.first;
        const QEOpServerProxy::AggRowT& narows = it->second.second;

        MergeFullRow(ukey, uniks, narows, output);

    }
}

StatsSelect::StatsSelect(AnalyticsQuery * m_query,
		const std::vector<std::string> & select_fields) :
			main_query(m_query), select_fields_(select_fields),
			ts_period_(0), isT_(false), count_field_() {

    QE_ASSERT(main_query->is_stat_table_query(main_query->table()));
    status_ = false;

    for (size_t j=0; j<select_fields_.size(); j++) {

        if (select_fields_[j] == g_viz_constants.STAT_TIME_FIELD) {
            if (ts_period_) {
                QE_LOG(INFO, "StatsSelect cannot accept both T and T="); 
                return;
            }
            isT_ = true;
            sort_cols_.insert(std::make_pair(select_fields_[j], 0));
            QE_TRACE(DEBUG, "StatsSelect T");

        } else if (select_fields_[j].substr(0, g_viz_constants.STAT_TIMEBIN_FIELD.size()) ==
                g_viz_constants.STAT_TIMEBIN_FIELD) {
            if (isT_) {
                QE_LOG(INFO, "StatsSelect cannot accept both T and T="); 
                return;
            }
            string tsstr = select_fields_[j].substr(g_viz_constants.STAT_TIMEBIN_FIELD.size());
            ts_period_ = strtoul(tsstr.c_str(), NULL, 10) * 1000000;
            if (!ts_period_) {
                QE_LOG(INFO, "StatsSelect cannot accept T= for " << tsstr);
                return;
            }
            sort_cols_.insert(std::make_pair(g_viz_constants.STAT_TIMEBIN_FIELD,0));
            QE_TRACE(DEBUG, "StatsSelect T=");
        } else {
            std::string sfield = select_fields_[j];
            QEOpServerProxy::AggOper agg =
                StatsQuery::ParseAgg(select_fields_[j], sfield);
            if (agg == QEOpServerProxy::INVALID) {
                unik_cols_.insert(select_fields_[j]);
                QE_TRACE(DEBUG, "StatsSelect unik field " << select_fields_[j]);
            } else if (agg == QEOpServerProxy::COUNT) {
                count_field_ = sfield;
                QE_TRACE(DEBUG, "StatsSelect COUNT " << sfield);
            } else if (agg == QEOpServerProxy::SUM) {
                sum_cols_.insert(sfield);
                QE_TRACE(DEBUG, "StatsSelect SUM " << sfield);
            } else if (agg == QEOpServerProxy::CLASS) {
                class_cols_.insert(sfield);
                QE_TRACE(DEBUG, "StatsSelect CLASS " << sfield);
            } else if (agg == QEOpServerProxy::MAX) {
                max_field_.insert(sfield);
                QE_TRACE(DEBUG, "StatsSelect MAX " << sfield);
            } else if (agg == QEOpServerProxy::MIN) {
                min_field_.insert(sfield);
                QE_TRACE(DEBUG, "StatsSelect MIN " << sfield);
            } else if (agg == QEOpServerProxy::PERCENTILES) {
                percentile_cols_.insert(sfield);
                QE_TRACE(DEBUG, "StatsSelect PERCENTILES " << sfield);
            } else if (agg == QEOpServerProxy::AVG) {
                avg_field_.insert(sfield);
                QE_TRACE(DEBUG, "StatsSelect AVG " << sfield);
            } else {
                QE_ASSERT(0);
            }
        }
    }

    status_ = true;
}

void StatsSelect::SetSortOrder(const std::vector<sort_field_t>& sort_fields) {
    if (sort_fields.size()) {
        sort_cols_.clear();
    } else {
        QE_TRACE(DEBUG, "StatsSelect Sort By T/T=");
    }
    for (size_t st = 0; st < sort_fields.size(); st++) {

        if (sort_fields[st].name.substr(0, g_viz_constants.STAT_TIMEBIN_FIELD.size()) ==
                g_viz_constants.STAT_TIMEBIN_FIELD) {
            QE_TRACE(DEBUG, "StatsSelect Sort By T=");
            sort_cols_.insert(std::make_pair(g_viz_constants.STAT_TIMEBIN_FIELD,st));
        } else if (sort_fields[st].name == g_viz_constants.STAT_TIME_FIELD) {
            QE_TRACE(DEBUG, "StatsSelect Sort By " << sort_fields[st].name);
            sort_cols_.insert(std::make_pair(sort_fields[st].name, st));                
        } else {
            std::string sfield = sort_fields[st].name;
            QEOpServerProxy::AggOper agg =
                StatsQuery::ParseAgg(sort_fields[st].name, sfield);
            if (main_query->stats().is_stat_table_static()) {
                StatsQuery::column_t c = main_query->stats().get_column_desc(sfield);
                if (c.datatype == QEOpServerProxy::BLANK) {
                    QE_LOG(INFO, "StatsSelect unknown field " << sort_fields[st].name);
                    return;
                }
            }
            if (agg == QEOpServerProxy::INVALID) {
                QE_TRACE(DEBUG, "StatsSelect Sort By " << sort_fields[st].name);
                sort_cols_.insert(std::make_pair(sort_fields[st].name,st));                
            } else  {
                agg_sort_cols_.insert(make_pair(make_pair(agg, sfield), st));
                QE_TRACE(DEBUG, "StatsSelect Sort Agg " << agg << " of " << sfield);
            }
        }
    }    
}

void StatsSelect::MergeFullRow(
        const std::vector<StatVal>& ukey,
        const StatMap& uniks,
        const QEOpServerProxy::AggRowT& narows,
        MapBufT& output) {

    MapBufT::iterator rt = output.find(ukey);
    if (rt!= output.end()) {
        StatMap& rm = rt->second.first;
        if (rm!=uniks) {
            output.insert(make_pair(ukey, make_pair(uniks, narows)));
        } else {
            QEOpServerProxy::AggRowT &arows = rt->second.second;
            MergeAggRow(arows, narows);
        }
    } else {
        QEOpServerProxy::AggRowT arows;
        output.insert(make_pair(ukey, make_pair(uniks, narows)));
    }    
}


void StatsSelect::MergeAggRow(QEOpServerProxy::AggRowT &arows,
        const QEOpServerProxy::AggRowT &narows) {
    for (QEOpServerProxy::AggRowT::iterator jt = arows.begin();
            jt!= arows.end(); jt++) {
        QEOpServerProxy::AggRowT::const_iterator kt = narows.find(jt->first);
        if (kt!=narows.end()) {
            // Attribute name must match for aggregate and for the new value
            QE_ASSERT(jt->first.second == kt->first.second);

            if (jt->first.first == QEOpServerProxy::SUM) {
                StatsSelect::StatVal & sv = jt->second;
                try {
                    if (sv.which() == QEOpServerProxy::UINT64) {
                        sv = boost::get<uint64_t>(sv) + boost::get<uint64_t>(kt->second);
                    }
                    if (sv.which() == QEOpServerProxy::DOUBLE) {
                        sv = boost::get<double>(sv) + boost::get<double>(kt->second);
                    }                   
                } catch (boost::bad_get& ex) {
                    QE_ASSERT(0);
                } catch (const std::out_of_range& oor) {
                    QE_ASSERT(0);
                }                        

            }
            if (jt->first.first == QEOpServerProxy::COUNT) {
                try {
                    uint64_t& sv =  boost::get<uint64_t>(jt->second);
                    sv = sv + boost::get<uint64_t>(kt->second);
                } catch (boost::bad_get& ex) {
                    QE_ASSERT(0);
                } catch (const std::out_of_range& oor) {
                    QE_ASSERT(0);
                }     
            }
            if (jt->first.first == QEOpServerProxy::MAX) {
                StatsSelect::StatVal & sv = jt->second;
                try {
                    if (sv.which() == QEOpServerProxy::UINT64) {
                        uint64_t& existing_max = boost::get<uint64_t>(jt->second);
                        existing_max = (existing_max > (boost::get<uint64_t>(kt->second))) ? existing_max:(boost::get<uint64_t>(kt->second));
                    }
                    if (sv.which() == QEOpServerProxy::DOUBLE) {
                        double& existing_max = boost::get<double>(jt->second);
                        existing_max = (existing_max > (boost::get<double>(kt->second))) ? existing_max:(boost::get<double>(kt->second));
                    }                   
                } catch (boost::bad_get& ex) {
                    QE_ASSERT(0);
                }     
            }
            if (jt->first.first == QEOpServerProxy::MIN) {
                StatsSelect::StatVal & sv = jt->second;
                try {
                    if (sv.which() == QEOpServerProxy::UINT64) {
                        uint64_t& existing_min = boost::get<uint64_t>(jt->second);
                        existing_min = (existing_min < (boost::get<uint64_t>(kt->second))) ? existing_min:(boost::get<uint64_t>(kt->second));
                    }
                    if (sv.which() == QEOpServerProxy::DOUBLE) {
                        double& existing_min = boost::get<double>(jt->second);
                        existing_min = (existing_min < (boost::get<double>(kt->second))) ? existing_min:(boost::get<double>(kt->second));
                    }                   
                } catch (boost::bad_get& ex) {
                    QE_ASSERT(0);
                }     
            }
            if (jt->first.first == QEOpServerProxy::AVG) {
                StatsSelect::StatVal & sv = jt->second;
                try {
                    if (sv.which() == QEOpServerProxy::CENTROID) {
                        boost::shared_ptr<Centroid>& pt =
                            boost::get<boost::shared_ptr<Centroid> >(jt->second);

                        boost::shared_ptr<Centroid> pt2 =
                            boost::get<boost::shared_ptr<Centroid> >(kt->second);
                        Centroid_add(pt.get(),
                                Centroid_get_mean(pt2.get()),
                                Centroid_get_count(pt2.get()));
                    }
                } catch (boost::bad_get& ex) {
                    QE_ASSERT(0);
                }
            }
            if (jt->first.first == QEOpServerProxy::PERCENTILES) {
                StatsSelect::StatVal & sv = jt->second;
                try {
                    if (sv.which() == QEOpServerProxy::TDIGEST) {
                        boost::shared_ptr<TDigest>& pt =
                            boost::get<boost::shared_ptr<TDigest> >(jt->second);
                        
                        boost::shared_ptr<TDigest> pt2 =
                            boost::get<boost::shared_ptr<TDigest> >(kt->second);
                        size_t j, ncentroids = TDigest_get_ncentroids(pt2.get());
                        for (j = 0; j < ncentroids; j++) {
                            Centroid * c = TDigest_get_centroid(pt2.get(), j);
                            TDigest* nd = TDigest_add(pt.get(),
                                Centroid_get_mean(c),
                                Centroid_get_count(c));
                            if (nd) {
                                pt.reset(nd);
                            }
                        }
                    }
                } catch (boost::bad_get& ex) {
                    QE_ASSERT(0);
                }     
            }
        }
    }
}

namespace boost {
   std::size_t hash_value(const StatsSelect::StatVal&); 
}

std::size_t boost::hash_value(const StatsSelect::StatVal& sv) {
    std::ostringstream ostr;
    ostr << sv;
    return boost::hash_value(ostr.str());
}

bool StatsSelect::LoadRow(boost::uuids::uuid u,
		uint64_t timestamp, const vector<StatEntry>& row, MapBufT& output) {

	if (!Status()) return false;
    uint64_t ts = 0;

    // Build Uniks map
    StatMap uniks;
    set<string>::const_iterator ukit =
        unik_cols_.find(g_viz_constants.STAT_UUID_FIELD);
    if (ukit!=unik_cols_.end()) {
        uniks.insert(make_pair(g_viz_constants.STAT_UUID_FIELD,u));
    }
    
    if (isT_) {
        ts = timestamp;
        uniks.insert(make_pair(g_viz_constants.STAT_TIME_FIELD,ts)); 
    }

    if (ts_period_) {
        ts = timestamp - (timestamp % ts_period_);
        uniks.insert(make_pair(g_viz_constants.STAT_TIMEBIN_FIELD,ts)); 
    }

    for (vector<StatEntry>::const_iterator it = row.begin();
            it != row.end(); it++) {
        set<string>::const_iterator uit = unik_cols_.find(it->name);
        if (uit!=unik_cols_.end()) {
            uniks.insert(make_pair(it->name, it->value));
        }
    }

    // Build sort vector
    // Last slot is reserved for the hash
    std::vector<StatVal> ukey(sort_cols_.size() + agg_sort_cols_.size() + 1);
    size_t hash_slot = sort_cols_.size() + agg_sort_cols_.size();
    uint64_t hash_val = boost::hash_range(uniks.begin(), uniks.end());
    ukey[hash_slot] = hash_val;

    for (map<string, size_t>::const_iterator st = sort_cols_.begin();
            st!=sort_cols_.end(); st++) {
        QE_ASSERT(uniks.find(st->first) != uniks.end());
        ukey[st->second] = uniks.at(st->first);
    }

    QEOpServerProxy::AggRowT narows;
    for (vector<StatEntry>::const_iterator it = row.begin();
            it != row.end(); it++) {
        set<string>::const_iterator uit = sum_cols_.find(it->name);
        if (uit!=sum_cols_.end()) {
            pair<QEOpServerProxy::AggOper,string> aggkey(QEOpServerProxy::SUM,it->name);
            narows.insert(make_pair(aggkey, it->value)); 
        }
    }

    for (vector<StatEntry>::const_iterator it = row.begin();
            it != row.end(); it++) {
        set<string>::const_iterator uit = max_field_.find(it->name);
        if (uit!=max_field_.end()) {
            pair<QEOpServerProxy::AggOper,string> aggkey(QEOpServerProxy::MAX,it->name);
            narows.insert(make_pair(aggkey, it->value)); 
        }
        uit = min_field_.find(it->name);
        if (uit!=min_field_.end()) {
            pair<QEOpServerProxy::AggOper,string> aggkey(QEOpServerProxy::MIN,it->name);
            narows.insert(make_pair(aggkey, it->value)); 
        }
        uit = avg_field_.find(it->name);
        if (uit!=avg_field_.end()) {
            pair<QEOpServerProxy::AggOper,string> aggkey(QEOpServerProxy::AVG,it->name);
            try {
                Centroid *t = Centroid_create(0, 0);
                StatsSelect::StatVal sv = it->value;
                double val;
                if (sv.which() == QEOpServerProxy::UINT64) {
                    val = boost::get<uint64_t>(sv);
                } else {
                    val = boost::get<double>(sv);
                }
                Centroid_add(t, val, 1);
                boost::shared_ptr<Centroid> pt(t, &StatsSelect::DeleteCentroid);
                narows.insert(make_pair(aggkey, pt));
            } catch (boost::bad_get& ex) {
                QE_ASSERT(0);
            } catch (const std::out_of_range& oor) {
                QE_ASSERT(0);
            }
        }
        uit = percentile_cols_.find(it->name);
        if (uit!=percentile_cols_.end()) {
            pair<QEOpServerProxy::AggOper,string> aggkey(QEOpServerProxy::PERCENTILES,it->name);

	    try {
		TDigest *t = TDigest_create(0.01, 100);
		StatsSelect::StatVal sv = it->value;
		double val;
		if (sv.which() == QEOpServerProxy::UINT64) {
                    val = boost::get<uint64_t>(sv);
		} else {
                    val = boost::get<double>(sv);
		}                   
		TDigest_add(t, val, 1);
		boost::shared_ptr<TDigest> pt(t, &StatsSelect::DeleteTDigest);
		narows.insert(make_pair(aggkey, pt)); 
	    } catch (boost::bad_get& ex) {
		QE_ASSERT(0);
	    } catch (const std::out_of_range& oor) {
		QE_ASSERT(0);
	    }                        
        }
    }
    
    for (std::set<std::string>::const_iterator ct = class_cols_.begin();
            ct!=class_cols_.end(); ct++) {
        pair<QEOpServerProxy::AggOper,string> aggkey(QEOpServerProxy::CLASS,*ct);
        StatMap huniks;
        for (vector<StatEntry>::const_iterator rit = row.begin();
                rit != row.end(); rit++) {
            if (rit->name != *ct) {
                if (uniks.find(rit->name) != uniks.end()) {
                    // For generating the hash, consider all attributes that 
                    // are in the row, and that do not match the CLASS attribute,
                    // and that are in non-aggregate attributes in the SELECT
                    huniks[rit->name] = rit->value;
                }
            }
        }
        uint64_t hh = boost::hash_range(huniks.begin(), huniks.end());
        narows.insert(make_pair(aggkey, hh));
    }
 
    if (!count_field_.empty()) {
        pair<QEOpServerProxy::AggOper,string> aggkey(QEOpServerProxy::COUNT,count_field_);
        narows.insert(make_pair(aggkey, (uint64_t) 1));            
    }
    
    MergeFullRow(ukey, uniks, narows, output);

    return true;
}

