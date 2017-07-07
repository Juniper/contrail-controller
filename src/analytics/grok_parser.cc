/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include "grok_parser.h"
#include <iostream>
#include <cstring>
#include <boost/algorithm/string.hpp>
#include <boost/regex.hpp>
#include <vector>
#include <list>
#include <base/logging.h>
#include <tbb/mutex.h>

tbb::mutex mutex;
tbb::mutex msglist_mutex;

GrokParser::GrokParser() {
    init();
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
std::string GrokParser::match(std::string strin) {
    tbb::mutex::scoped_lock lock(mutex);
    std::string msg_type;
    for (GrokMap::iterator it = grok_list_.begin(); it != grok_list_.end(); it++) {
        grok_match_t gm;
        if (grok_exec(&(it->second), strin.c_str(), &gm) == GROK_OK) {
            msg_type = it->first;
            /* construct map */
            tbb::mutex::scoped_lock lock_m(msglist_mutex);
            grok_match_walk_init(&gm);
            matched_msg_list_[strin] = std::make_pair(msg_type, gm);
            return msg_type;
        }
    }
    return msg_type;
}

/* get_matched_data()
   get matched content for a message type
   @Input - s: message type to match
            m: map to store matched content
*/
void GrokParser::get_matched_data(std::string strin, std::map<std::string, std::string>* m) {
    if (m == NULL)
        return;
    tbb::mutex::scoped_lock lock_m(msglist_mutex); 
    MatchedMsgList::iterator it = matched_msg_list_.find(strin);
    if (it != matched_msg_list_.end()) {
        (*m)["Message Type"] = it->second.first;
//        std::cout<< "Message Type: " << it->second.first << std::endl;
        char *prev_name = NULL;
        const char *prev_substr = NULL;
        char *name = NULL;
        int namelen(0);
        const char *substr = NULL;
        int substrlen(0);
        while (true) {
            grok_match_walk_next(&(it->second.second), &name, &namelen, &substr, &substrlen);
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
                    LOG(DEBUG, __func__ << name_str << " ==> " << substr_str);
                    (*m)[name_str] = substr_str;
                }
            }
            else {
                name_str = results[1];
                LOG(DEBUG, __func__ << name_str << " ==> " << substr_str);
                (*m)[name_str] = substr_str;
            }
            prev_name = name;
            prev_substr = substr;
        }  
    }
}

void GrokParser::set_named_capture_only(bool b) {
    named_capture_only_ = b;
}
