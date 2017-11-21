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
#include "analytics_types.h"


GrokParser::GrokParser() {
}

GrokParser::~GrokParser() {
    for (GrokMap::iterator it = grok_list_.begin(); it != grok_list_.end(); it++) {
        grok_free(it->second);
        free(it->second);
    }
}

/* init() -- Initialize GrokParser */
void GrokParser::init() {
    /* Load base pattern from file */
    //grok_patterns_import_from_file(base_, "/etc/contrail/grok-pattern-base.conf");
    named_capture_only_ = true;
    init_base_pattern();
}

/* create_grok_instance()
*  Add a base pattern to base_grok
*  @Input - name: grok instance name
*/
void GrokParser::create_grok_instance(std::string name) {
    if (grok_list_.find(name) != grok_list_.end()) {
        return;
    }
    
    /* add to map */
    grok_list_[name] = grok_new();
    install_base_pattern(name);
    LOG(DEBUG, __func__ << ": Syntax <" << name << "> added.");
    return;
}

/* add_pattern()
*  Add a base pattern to base_grok
*  @Input - name: grok instance name, "" means base_ grok
*           pattern: input pattern w/ format <NAME regex>
*/
void GrokParser::add_pattern(std::string name, std::string pattern) {
    grok_t *grok;
    if (grok_list_.find(name) != grok_list_.end()) {
        grok = grok_list_[name];
    } else {
        LOG(ERROR, "no grok instance " << name << " when add pattern");
        return;
    }
    grok_patterns_import_from_string(grok, pattern.c_str());
}

/* compile_pattern()
*  Compile pattern suite
*  @Input - s: grok instance name
*  @Return - true: created
*          - false: non-exist pattern or invalid pattern
*/
bool GrokParser::compile_pattern(std::string name) {
    grok_t *grok;
    if (grok_list_.find(name) == grok_list_.end()) {
        LOG(ERROR, "no grok instance " << name << " when compile");
        return false;
    }
    /* try compile */
    grok = grok_list_[name];
    if (GROK_OK != grok_compile(grok, std::string("%{" + name + "}").c_str())) {
        grok_free(grok);
        free(grok);
        grok_list_[name] = grok_new();
        install_base_pattern(name);
        LOG(ERROR, __func__ << ": Failed to create grok instance. Syntax INVALID.");
        return false;
    }

    return true;
}

/* del_grok_instance()
*  Delete message type and corresponding grok instance 
*  @Input - s; grok instance name
*  @Return - true: created
*          - false: non-exist pattern or invalid pattern
*/
bool GrokParser::del_grok_instance(std::string name) {
    GrokMap::iterator it = grok_list_.find(name);
    if (it == grok_list_.end()) {
        LOG(DEBUG, "no grok instance " << name << " when delete"); 
        return false;
    }
    grok_free(it->second);
    free(it->second);
    grok_list_.erase(it);
    return true;
}

/* match()
*  Match input message against all stored message type patterns and identify message type
*  @Input - strin: input message
                m: map to store matched content
*/
bool GrokParser::match(std::string name,
                       std::string strin, std::map<std::string, std::string>* m) {
    grok_t *grok;
    if (grok_list_.find(name) != grok_list_.end()) {
        grok = grok_list_[name];
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

std::vector<std::string> base_pattern;
void GrokParser::init_base_pattern() {
    base_pattern.push_back
    ("USERNAME [a-zA-Z0-9._-]+");
    base_pattern.push_back
    ("USER %{USERNAME}");
    base_pattern.push_back
    ("INT (?:[+-]?(?:[0-9]+))");
    base_pattern.push_back
    ("BASE10NUM (?<![0-9.+-])(?>[+-]?(?:(?:[0-9]+(?:\\.[0-9]+)?)|(?:\\.[0-9]+)))");
    base_pattern.push_back
    ("NUMBER (?:%{BASE10NUM})");
    base_pattern.push_back
    ("BASE16NUM (?<![0-9A-Fa-f])(?:[+-]?(?:0x)?(?:[0-9A-Fa-f]+))");
    base_pattern.push_back
    ("BASE16FLOAT \\b(?<![0-9A-Fa-f.])(?:[+-]?(?:0x)?(?:(?:[0-9A-Fa-f]+(?:\\.[0-9A-Fa-f]*)?)|(?:\\.[0-9A-Fa-f]+)))\\b");
    base_pattern.push_back
    ("POSINT \\b(?:[1-9][0-9]*)\\b");
    base_pattern.push_back
    ("NONNEGINT \\b(?:[0-9]+)\\b");
    base_pattern.push_back
    ("WORD \\b\\w+\\b");
    base_pattern.push_back
    ("NOTSPACE \\S+");
    base_pattern.push_back
    ("SPACE \\s*");
    base_pattern.push_back
    ("DATA .*?");
    base_pattern.push_back
    ("GREEDYDATA .*");
    base_pattern.push_back
    ("UUID [A-Fa-f0-9]{8}-(?:[A-Fa-f0-9]{4}-){3}[A-Fa-f0-9]{12}");

    // Networking
    base_pattern.push_back
    ("IPV6 ((([0-9A-Fa-f]{1,4}:){7}([0-9A-Fa-f]{1,4}|:))|(([0-9A-Fa-f]{1,4}:){6}(:[0-9A-Fa-f]{1,4}|((25[0-5]|2[0-4]"
           "\\d|1\\d\\d|[1-9]?\\d)(\\.(25[0-5]|2[0-4]\\d|1\\d\\d|[1-9]?\\d)){3})|:))|(([0-9A-Fa-f]{1,4}:){5}(((:[0-9"
           "A-Fa-f]{1,4}){1,2})|:((25[0-5]|2[0-4]\\d|1\\d\\d|[1-9]?\\d)(\\.(25[0-5]|2[0-4]\\d|1\\d\\d|[1-9]?\\d)){3})"
           "|:))|(([0-9A-Fa-f]{1,4}:){4}(((:[0-9A-Fa-f]{1,4}){1,3})|((:[0-9A-Fa-f]{1,4})?:((25[0-5]|2[0-4]\\d|1\\d\\d"
           "|[1-9]?\\d)(\\.(25[0-5]|2[0-4]\\d|1\\d\\d|[1-9]?\\d)){3}))|:))|(([0-9A-Fa-f]{1,4}:){3}(((:[0-9A-Fa-f]{1,4"
           "}){1,4})|((:[0-9A-Fa-f]{1,4}){0,2}:((25[0-5]|2[0-4]\\d|1\\d\\d|[1-9]?\\d)(\\.(25[0-5]|2[0-4]\\d|1\\d\\d|["
           "1-9]?\\d)){3}))|:))|(([0-9A-Fa-f]{1,4}:){2}(((:[0-9A-Fa-f]{1,4}){1,5})|((:[0-9A-Fa-f]{1,4}){0,3}:((25[0-5"
           "]|2[0-4]\\d|1\\d\\d|[1-9]?\\d)(\\.(25[0-5]|2[0-4]\\d|1\\d\\d|[1-9]?\\d)){3}))|:))|(([0-9A-Fa-f]{1,4}:){1}"
           "(((:[0-9A-Fa-f]{1,4}){1,6})|((:[0-9A-Fa-f]{1,4}){0,4}:((25[0-5]|2[0-4]\\d|1\\d\\d|[1-9]?\\d)(\\.(25[0-5]|"
           "2[0-4]\\d|1\\d\\d|[1-9]?\\d)){3}))|:))|(:(((:[0-9A-Fa-f]{1,4}){1,7})|((:[0-9A-Fa-f]{1,4}){0,5}:((25[0-5]|"
           "2[0-4]\\d|1\\d\\d|[1-9]?\\d)(\\.(25[0-5]|2[0-4]\\d|1\\d\\d|[1-9]?\\d)){3}))|:)))(%.+)?");
    base_pattern.push_back
    ("IPV4 (?<![0-9])(?:(?:25[0-5]|2[0-4][0-9]|[0-1]?[0-9]{1,2})[.](?:25[0-5]|2[0-4][0-9]|[0-1]?[0-9]{1,2})[.](?:25["
           "0-5]|2[0-4][0-9]|[0-1]?[0-9]{1,2})[.](?:25[0-5]|2[0-4][0-9]|[0-1]?[0-9]{1,2}))(?![0-9])");
    base_pattern.push_back
    ("IP (?:%{IPV6}|%{IPV4})");
    base_pattern.push_back
    ("HOSTNAME \\b(?:[0-9A-Za-z][0-9A-Za-z-]{0,62})(?:\\.(?:[0-9A-Za-z][0-9A-Za-z-]{0,62}))*(\\.?|\\b)");
    base_pattern.push_back
    ("HOST %{HOSTNAME");
    base_pattern.push_back
    ("IPORHOST (?:%{HOSTNAME}|%{IP})");
    base_pattern.push_back
    ("HOSTPORT (?:%{IPORHOST=~/\\./}:%{POSINT})");

    // Months: January, Feb, 3, 03, 12, December
    base_pattern.push_back
    ("MONTH \\b(?:Jan(?:uary)?|Feb(?:ruary)?|Mar(?:ch)?|Apr(?:il)?|May|Jun(?:e)?|Jul(?:y)?|Aug(?:ust)?"
                 "|Sep(?:tember)?|Oct(?:ober)?|Nov(?:ember)?|Dec(?:ember)?)\\b");
    base_pattern.push_back
    ("MONTHNUM (?:0?[1-9]|1[0-2])");
    base_pattern.push_back
    ("MONTHDAY (?:(?:0[1-9])|(?:[12][0-9])|(?:3[01])|[1-9])");

    //Days: Monday, Tue, Thu, etc..
    base_pattern.push_back
    ("DAY (?:Mon(?:day)?|Tue(?:sday)?|Wed(?:nesday)?|Thu(?:rsday)?|Fri(?:day)?|Sat(?:urday)?|Sun(?:day)?)");

    //Years?
    base_pattern.push_back
    ("YEAR (?>\\d\\d){1,2}");
    base_pattern.push_back
    ("HOUR (?:2[0123]|[01]?[0-9])");
    base_pattern.push_back
    ("MINUTE (?:[0-5][0-9])");
    // '60' is a leap second in most time standards and thus is valid.
    base_pattern.push_back
    ("SECOND (?:(?:[0-5][0-9]|60)(?:[:.,][0-9]+)?)");
    base_pattern.push_back
    ("TIME (?!<[0-9])%{HOUR}:%{MINUTE}(?::%{SECOND})(?![0-9])");
    // datestamp is YYYY/MM/DD-HH:MM:SS.UUUU (or something like it)
    base_pattern.push_back
    ("DATE_US %{MONTHNUM}[/-]%{MONTHDAY}[/-]%{YEAR}");
    base_pattern.push_back
    ("DATE_EU %{MONTHDAY}[./-]%{MONTHNUM}[./-]%{YEAR}");
    base_pattern.push_back
    ("ISO8601_TIMEZONE (?:Z|[+-]%{HOUR}(?::?%{MINUTE}))");
    base_pattern.push_back
    ("ISO8601_SECOND (?:%{SECOND}|60)");
    base_pattern.push_back
    ("TIMESTAMP_ISO8601 %{YEAR}-%{MONTHNUM}-%{MONTHDAY}[T ]%{HOUR}:?%{MINUTE}(?::?%{SECOND})?%{ISO8601_TIMEZONE}?");
    base_pattern.push_back
    ("DATE %{DATE_US}|%{DATE_EU}");
    base_pattern.push_back
    ("DATESTAMP %{DATE}[- ]%{TIME}");
    base_pattern.push_back
    ("TZ (?:[PMCE][SD]T|UTC)");
    base_pattern.push_back
    ("DATESTAMP_RFC822 %{DAY} %{MONTH} %{MONTHDAY} %{YEAR} %{TIME} %{TZ}");
    base_pattern.push_back
    ("DATESTAMP_OTHER %{DAY} %{MONTH} %{MONTHDAY} %{TIME} %{TZ} %{YEAR}");
}

void GrokParser::install_base_pattern(std::string name) {
    grok_t *grok;
    if (grok_list_.find(name) != grok_list_.end()) {
        grok = grok_list_[name];
    } else {
        LOG(ERROR, "no grok instance " << name << " when add pattern");
        return;
    }
    for (uint32_t i = 0; i < base_pattern.size(); i++) {
        grok_patterns_import_from_string(grok, base_pattern[i].c_str());
    }
}

std::vector<std::string> GrokParser::get_base_pattern() {
    return base_pattern;
}
