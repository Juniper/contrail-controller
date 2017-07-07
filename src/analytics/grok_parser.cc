/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include "grok_parser.h"
#include <iostream>
#include <cstring>
#include <boost/algorithm/string.hpp>
#include <boost/regex.hpp>
#include <vector>
#include <base/logging.h>
#include <tbb/mutex.h>
#include <boost/lexical_cast.hpp>
#include "analytics_types.h"

tbb::mutex mutex;
tbb::mutex msglist_mutex;

GrokParser::GrokParser() {
}

GrokParser::~GrokParser() {
    tbb::mutex::scoped_lock lock(mutex);
    for (GrokMap::iterator it = grok_list_.begin(); it != grok_list_.end(); it++) {
        grok_free_clone(&(it->second));
    }
    grok_free(base_);
    free(base_);
}

/* init() -- Initialize GrokParser */
void GrokParser::init() {
    base_ = grok_new();
    /* Load base pattern from file */
    grok_patterns_import_from_file(base_, "/etc/contrail/grok-pattern-base.conf");
    named_capture_only_ = true;
}

/* add_base_pattern()
*  Add a base pattern to base_grok
*  @Input - pattern: input pattern w/ format <NAME regex>
*/
void GrokParser::add_base_pattern(std::string pattern) {
    grok_patterns_import_from_string(base_, pattern.c_str());
}

/* msg_type_add()
*  Create a new grok instance with defined message type and add to map
*  @Input - s: Syntax only
*  @Return - true: created
*          - false: non-exist pattern or invalid pattern
*/
bool GrokParser::msg_type_add(std::string s) {
    const char *regexp = NULL;
    size_t len = 0;
    /* find message type in base pattern */
    grok_pattern_find(base_, s.c_str(), s.length(), &regexp, &len);
    if (regexp == NULL) {
        LOG(DEBUG, __func__ << ": Failed to create grok instance. Syntax NOT DEFINED.");
        return false;
    }

    /* Assign grok->patterns to the subtree */
    grok_t grok;
    grok_clone(&grok, base_);

    /* try compile */
    if (GROK_OK != grok_compile(&grok, std::string("%{" + s + "}").c_str())) {
        grok_free_clone(&grok);
        LOG(DEBUG, __func__ << ": Failed to create grok instance. Syntax INVALID.");
        return false;
    }

    /* add to map */
    tbb::mutex::scoped_lock lock(mutex);
    grok_list_[s] = grok;
    lock.release();
    LOG(DEBUG, __func__ << ": Syntax <" << s << "> added."); 
    return true;
}

/* msg_type_del()
*  Delete message type and corresponding grok instance 
*  @Input - s; message type to delete
*/
bool GrokParser::msg_type_del(std::string s) {
    tbb::mutex::scoped_lock lock(mutex);
    GrokMap::iterator it = grok_list_.find(s);
    if (it == grok_list_.end()) {
        lock.release();
        LOG(DEBUG, __func__ << ": Failed to delete. Grok instance with provided message type does not exist."); 
        return false;
    }
    grok_free_clone(&(it->second));
    grok_list_.erase(it);
    return true;
}

/* match()
*  Match input message against all stored message type patterns and identify message type
*  @Input - strin: input message
                m: map to store matched content
*/
bool GrokParser::match(std::string strin, std::map<std::string, std::string>* m) {
    tbb::mutex::scoped_lock lock(mutex);
    std::string msg_type;
    for (GrokMap::iterator it = grok_list_.begin(); it != grok_list_.end(); it++) {
        grok_match_t gm;
        if (grok_exec(&(it->second), strin.c_str(), &gm) == GROK_OK) {
            msg_type = it->first;
            /* construct map */
            //tbb::mutex::scoped_lock lock_m(msglist_mutex);
            if (m == NULL) return true;
            grok_match_walk_init(&gm);
            //matched_msg_list_[strin] = std::make_pair(msg_type, gm);
            (*m)["Message Type"] = msg_type;
            char *prev_name = NULL;
            const char *prev_substr = NULL;
            char *name = NULL;
            int namelen(0);
            const char *substr = NULL;
            int substrlen(0);
            while (true) {
                grok_match_walk_next(&gm, &name, &namelen, &substr, &substrlen);
                if (prev_name == name && prev_substr == substr) break;
                std::string name_str = std::string(name).substr(0, namelen);
                std::string substr_str = std::string(substr).substr(0, substrlen);
                std::vector<std::string> results;
                boost::split(results, name_str, boost::is_any_of(":"));
                if (results.size() == 1) {
                    if (named_capture_only_) {
                        prev_name = name;
                        prev_substr = substr;
                        continue;
                    }
                    else {
                        (*m)[name_str] = substr_str;
                    }
                }
                else {
                    name_str = results[1];
                    (*m)[name_str] = substr_str;
                }
                prev_name = name;
                prev_substr = substr;
            }
            return true;
        }
    }
    return false;
}

void GrokParser::send_generic_stat(std::map<std::string, std::string> &m_in) {
    std::map<std::string, uint64_t> m_out;
    for (std::vector<std::string>::iterator ait = attribs_.begin(); ait != attribs_.end(); ait++) {
        std::string cur_attrib = (*ait);
        for (std::vector<std::string>::iterator kit = keys_.begin(); kit != keys_.end(); kit++) {
            std::string a_key = cur_attrib;
            std::string cur_keyval = (*kit);
            m_out[a_key + "." + cur_keyval] = boost::lexical_cast<uint64_t>(m_in[cur_attrib].c_str());
            cur_keyval += "." + m_in[(*kit)];
            a_key += "." + cur_keyval;
            m_out[a_key] = boost::lexical_cast<uint64_t>(m_in[cur_attrib].c_str());
        }
    }
    GenericStats * snh(GENERIC_STATS_CREATE());
    snh->set_name(attribs_[0]);
    snh->set_msg_type(m_in["Message Type"]);
    snh->set_attrib(m_out);
    GENERIC_STATS_SEND_SANDESH(snh);
}

void GrokParser::set_named_capture_only(bool b) {
    named_capture_only_ = b;
}

void GrokParser::set_key_list(const std::vector<std::string>& key) {
    keys_ = key;
}

void GrokParser::set_attrib_list(const std::vector<std::string>& attrib) {
    attribs_ = attrib;
}
