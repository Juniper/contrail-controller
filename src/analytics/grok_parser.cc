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
#include <boost/lexical_cast.hpp>

GrokParser::GrokParser() {
}

GrokParser::~GrokParser() {
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
void GrokParser::add_base_pattern(std::string name, std::string pattern) {
    grok_t grok;
    if (grok_list_.find(name) != grok_list_.end()) {
        grok = grok_list_[name];
    } else {
        return;
    }
    grok_patterns_import_from_string(&grok, pattern.c_str());
}

/* match()
*  Match input message against all stored message type patterns and identify message type
*  @Input - strin: input message
                m: map to store matched content
*/
bool GrokParser::match(std::string name,
                 std::string strin, std::map<std::string, std::string>* m) {
    grok_t *grok;
    if (name.empty()) {
        grok = base_;
    } else if (grok_list_.find(name) != grok_list_.end()) {
        grok = &grok_list_[name];
    } else {
        LOG(ERROR, "user defined pattern name %s do not exist" << name);
        return false;
    }
    grok_match_t gm;
    if (grok_exec(grok, strin.c_str(), &gm) == GROK_OK) {
        /* construct map */
        if (m == NULL) return true;
        grok_match_walk_init(&gm);
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
    return false;
}

void GrokParser::set_named_capture_only(bool b) {
    named_capture_only_ = b;
}

/* add_pattern()
*  Add and enable one usr defined syslog match pattern
*  @Input - name: pattern name
*           pattern: user defined pattern
*  @Output - True if success, else return false
*/
bool GrokParser::add_pattern(std::string name, std::string pattern) {
    /* Avoid name conflict with grok default pattern*/
    const char *regexp = NULL;
    size_t len = 0;
    grok_pattern_find(base_, name.c_str(), name.length(), &regexp, &len);
    if (regexp != NULL) {
        LOG(DEBUG, "user defined pattern name %s conflict with default" << name);
        return false;
    }

    /* Avoid name conflict with existing user defined pattern*/
    if (grok_list_.find(name) != grok_list_.end()) {
        LOG(DEBUG, "user defined pattern name %s has existed" << name);
        return true;
    }

    /* Assign grok->patterns to the subtree */
    grok_t grok;
    grok_clone(&grok, base_);
    if (pattern.empty()) {
        return true;
    }

    /* Add user defined pattern*/
    grok_pattern_add(&grok, name.c_str(), name.length(),
                            pattern.c_str(), pattern.length());

    /* try compile */
    if (GROK_OK != grok_compile(&grok, std::string("%{" + name + "}").c_str())) {
        grok_free_clone(&grok);
        LOG(DEBUG, __func__ << ": Failed to create grok instance. Syntax INVALID.");
        return false;
    }

     /* add to map */
    grok_list_[name] = grok;
    LOG(DEBUG, __func__ << ": Syntax <" << name << "> added.");
    return true;
}

/* del_pattern()
*  disable and delete one usr defined syslog match pattern
*  @Input - name: user defined pattern name
*           pattern: user defined pattern
*/
void GrokParser::del_pattern(std::string name) {
    /* disable match*/
    GrokMap::iterator it = grok_list_.find(name);
    if (it == grok_list_.end()) {
        LOG(DEBUG, __func__ << ": Failed to delete. Grok instance with provided message type does not exist.");
        return;
    }
    grok_free_clone(&(it->second));
    grok_list_.erase(it);
    return;
}

std::string GrokParser::get_pattern(std::string name) {
    grok_t grok;
    if (grok_list_.find(name) != grok_list_.end()) {
        grok = grok_list_[name];
    } else {
        return "";
    }

    const char *regexp = NULL;
    size_t len = 0;
    grok_pattern_find(&grok, name.c_str(), name.length(), &regexp, &len);
    if (regexp == NULL) {
        return "";
    }
    std::string pattern(regexp);
    return pattern;
}
