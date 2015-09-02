/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ANALYTICS_DIFFSTATS_H__
#define ANALYTICS_DIFFSTATS_H__

#include <vector>

template<typename PtrMapType, typename KeyType, typename ValueStatsType,
    typename OutputStatsType>
void GetDiffStats(PtrMapType &stats_map, PtrMapType &old_stats_map,
    std::vector<OutputStatsType> &v_output_stats) {
    // Send diffs 
    for (typename PtrMapType::const_iterator it = stats_map.begin();
         it != stats_map.end(); it++) {
         // Get new stats
         const ValueStatsType *nstats(it->second);
         KeyType key(it->first);
         typename PtrMapType::iterator oit = old_stats_map.find(key);
         // If entry does not exist in old map, insert it
         if (oit == old_stats_map.end()) {
             oit = (old_stats_map.insert(key, new ValueStatsType)).first;
         }
         // Get old stats
         ValueStatsType *ostats(oit->second);
         // Subtract the old from new
         ValueStatsType dstats(*nstats - *ostats);
         // Update old
         *ostats = *nstats;
         // Populate diff stats
         OutputStatsType output_stats;
         dstats.Get(key, output_stats);
         v_output_stats.push_back(output_stats);
    }
}

#endif  // ANALYTICS_DIFFSTATS_H__
