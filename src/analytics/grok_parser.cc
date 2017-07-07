/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

extern "C" {
   // #include <tcutil.h>
    #include <grok.h>
}

#include "grok_parser.h"
#include <vector>
#include <iostream>
#include <queue>
#include <string>
#include <cstring>
#include <boost/algorithm/string.hpp>
#include <boost/regex.hpp>
#include <map>

GrokParser::GrokParser() {
    parser_init();
}

GrokParser::~GrokParser() {
    grok_free(&grok_);
}

void GrokParser::parser_init() {
    grok_init(&grok_);
    grok_patterns_import_from_file(&grok_, "/etc/contrail/grok-pattern-base.conf");
    return;
}

void GrokParser::_pattern_parse_string(const char *line, const char**name, size_t *name_len,
                           const char **regexp, size_t *regexp_len) {
    size_t offset;

    /* Skip leading whitespace */
    offset = strspn(line, " \t");
    *name = line + offset;

    /* Find the first whitespace */
    offset += strcspn(line + offset, " \t");
    *name_len = offset - (*name - line);

    offset += strspn(line + offset, " \t");
    *regexp = line + offset;
    *regexp_len = strlen(line) - (*regexp - line);
}

void GrokParser::add_pattern_from_string(const char* buffer, PATTERN_TYPE t) {
    char *tokctx = NULL;
    char *tok = NULL;
    char *strptr = NULL;
    char *dupbuf = NULL;
    //grok_log(&grok_, LOG_PATTERNS, "Importing patterns from string");

    dupbuf = strdup(buffer);
    strptr = dupbuf;

    while ((tok = strtok_r(strptr, "\n", &tokctx)) != NULL) {
        const char *name, *regexp;
        size_t name_len, regexp_len;
        strptr = NULL;

        /* skip leading whitespace */
        tok += strspn(tok, " \t");

        /* If first non-whitespace is a '#', then this is a comment. */
        if (*tok == '#') continue;

        _pattern_parse_string(tok, &name, &name_len, &regexp, &regexp_len);
        (void) grok_pattern_add(&grok_, name, name_len, regexp, regexp_len);
        
        if (t == MSG) {
            mregex_map_[std::string(name).substr(0, name_len)] = std::string(regexp).substr(0, regexp_len);
        }
    }
    free(dupbuf);
    return;
}

/*
bool GrokParser::grok_parser_delete_pattern(std::string name) {
    return tctreeout((&grok_)->patterns, name.c_str(), name.length()) | mregex_map_.erase(name);
}
*/

int GrokParser::phrase_match(std::string pattern, std::string strin) {
    grok_match_t gm;
    int retval = grok_compile(&grok_, pattern.c_str());
    if (retval != GROK_OK) {
        return retval;
    }
    retval = grok_exec(&grok_, strin.c_str(), &gm);
    if (retval != GROK_OK) {
        return retval;
    }
    std::queue<std::string> q;
    match_enqueue(pattern, &q);
    const char *match;
    int len(0);
    while (!q.empty()) {
        grok_match_get_named_substring(&gm, q.front().c_str(), &match, &len);
        std::cout << q.front() << " ==> " << std::string(match).substr(0, len) << std::endl;
        q.pop();
    }
    return GROK_OK;
}

bool GrokParser::msg_match(std::string strin) {
    for (std::map<std::string, std::string>::iterator it = mregex_map_.begin();
        it != mregex_map_.end(); ++it) {
        if (this->phrase_match(it->second, strin) != GROK_OK) {
            continue;
        }
        return true;
    }
    return false;
}

void GrokParser::match_enqueue(std::string pattern, std::queue<std::string>* q) {
    boost::regex ex("%\\{(\\w+)(:(\\w+))?\\}");
    boost::smatch match;
    boost::sregex_token_iterator iter(pattern.begin(), pattern.end(), ex, 0);
    boost::sregex_token_iterator end;
    for (; iter != end; ++iter) {
        std::string r = *iter;
        std::vector<std::string> v;
        boost::split(v, r, boost::is_any_of(":"));
        if (v.size() == 1) {
            boost::algorithm::trim_left_if(v[0], boost::is_any_of("%{"));
            boost::algorithm::trim_right_if(v[0], boost::is_any_of("}"));
            q->push(v[0]);
        }
        else {
            //assert(v.size() == 2);
            boost::algorithm::trim_right_if(v[1], boost::is_any_of("}"));
            q->push(v[1]);
        }
    }
    return;
}

void GrokParser::list_msg_type() {
    for (std::map<std::string, std::string>::iterator it = mregex_map_.begin();
        it != mregex_map_.end(); ++it) {
        std::cout << it->first << " ==> " << it->second;
    }
    return;
}

void GrokParser::parser_free() {
    grok_free(&grok_);
    return;
}

