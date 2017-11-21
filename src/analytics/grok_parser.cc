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
    named_capture_only_ = true;
}

/* create_new_grok
*  create a grok instance with name
*  @Input - name: grok name
*/
bool GrokParser::create_new_grok(std::string name) {
    /* Avoid name conflict with grok default pattern*/
    const char *regexp = NULL;
    size_t len = 0;
    grok_pattern_find(base_, name.c_str(), name.length(), &regexp, &len);
    if (regexp != NULL) {
        LOG(DEBUG, "user defined pattern name "<< name <<" conflict with default");
        return false;
    }

    if (grok_list_.find(name) != grok_list_.end()) {
        LOG(DEBUG, "user defined pattern name %s has existed" << name);
        return false;
    }
    grok_t grok;
    grok_clone(&grok, base_);

    /* add to map */
    grok_list_[name] = grok;
    LOG(DEBUG, __func__ << ": Syntax <" << name << "> added.");
    return true;
}

/* delete_grok
*  delete a grok instance
*  @Input - name: grok name
*/
void GrokParser::delete_grok(std::string name) {
    GrokMap::iterator it = grok_list_.find(name);
    if (it == grok_list_.end()) {
        LOG(DEBUG, __func__ << ": Failed to delete. Grok instance with provided message type does not exist.");
        return;
    }
    grok_free_clone(&(it->second));
    grok_list_.erase(it);
    return;
}

/* add_pattern_from_string
*  Add pattern to existed grok with string.
*  important: full_pattern is "pattern_name + pattern".
*  this can be called multi times for same name. but at lease one full_pattern's
*  pattern_name is same to name, or compile pattern will fail.
*  @Input - name: one grok name which has been created.
*   full_pattern: pattern_name + " "(SPACE) + pattern. we can add a couple of 
*                 patterns for assinged grok, but at least one pattern_name is 
*                 same to grok name.
*/
void GrokParser::add_pattern_from_string(std::string name, std::string full_pattern) {
    grok_t grok;
    if (name.empty()) {
        grok = *base_;
    } else if (grok_list_.find(name) != grok_list_.end()) {
        grok = grok_list_[name];
    } else {
        return;
    }
    grok_patterns_import_from_string(&grok, full_pattern.c_str());
}

/* compile_pattern
*  compile grok pattern and prepare for match
*  @Input - name: grok name
*  @Output - True if success, else return false
*/
bool GrokParser::compile_pattern(std::string name) {
    grok_t grok;
    
    if (grok_list_.find(name) != grok_list_.end()) {
       grok = grok_list_[name];
    } else {
       return false;
    }
    /* try compile */
    if (GROK_OK != grok_compile(&grok, std::string("%{" + name + "}").c_str())) {
        LOG(ERROR, __func__ << ": Failed to compile grok instance. Syntax INVALID.");
        return false;
    }
    return true;
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

/* match()
*  Match input message against all stored message type patterns and identify message type
*  @Input -  name: grok name
            strin: input message
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
        LOG(ERROR, "user defined pattern name " << name << " do not exist");
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

