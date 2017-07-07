#include "grok_parser.h"
#include <iostream>
#include <cstring>
#include <boost/algorithm/string.hpp>
#include <boost/regex.hpp>
#include <vector>
#include <list>

GrokParser::GrokParser() {
    init();
}

GrokParser::~GrokParser() {
    for (std::map<std::string, std::pair<grok_t, std::list<std::string> > >::iterator it = grok_list_.begin(); it != grok_list_.end(); it++) {
      grok_free_clone(&(it->second.first));
    }
    grok_free(base_);
    free(base_);
}

void GrokParser::init() {
    base_ = grok_new();
    grok_patterns_import_from_file(base_, "/etc/contrail/grok-pattern-base.conf");
    break_if_match_ = false;
}     

void GrokParser::set_break_if_match(bool b) {
    break_if_match_ = b;
}

void GrokParser::add_base_pattern(std::string pattern) {
    grok_patterns_import_from_string(base_, pattern.c_str());
}

/* syntax_del()
*  Create a new grok instance with defined syntax and add to map
*  @Input - s: Syntax only
*  @Return - true: created
*          - false: non-exist pattern or invalid pattern
*/
bool GrokParser::syntax_add(std::string s) {
    const char *regexp = NULL;
    size_t len = 0;
    /* find syntax in base pattern */
    grok_pattern_find(base_, s.c_str(), s.length(), &regexp, &len);
    if (regexp == NULL) {
        std::cout << "[syntax_add:" << __LINE__ << "] > Failed to create grok instance. Syntax NOT DEFINED." << std::endl;
        return false;
    }
    
    /* Assign grok->patterns to the subtree */
    grok_t grok;
    grok_clone(&grok, base_);
    //s = "%{" + s + "}";
    /* try compile */
    if (GROK_OK != grok_compile(&grok, std::string("%{" + s + "}").c_str())) {
        grok_free_clone(&grok);
        std::cout << "[syntax_add:" << __LINE__ << "] > Failed to create grok instance. Syntax INVALID." << std::endl;
        return false;
    }
    std::list<std::string> q;
    _pattern_name_enlist(std::string(regexp), &q);
    /* n.size() can only be 2 or 1
     * n.size() == 2 : contains subname
     * n.size() == 1 : contains syntax only */
    grok_list_[s] = std::make_pair(grok, q);
    return true;
}

void GrokParser::_pattern_name_enlist(std::string s,
                              std::list<std::string>* q) {
    // regex
    boost::regex ex("%\\{(\\w+)(:(\\w+))?\\}");
    boost::smatch match;
    boost::sregex_token_iterator iter(s.begin(), s.end(), ex, 0);
    boost::sregex_token_iterator end;
    /*
    for (; iter != end; ++iter) {
            std::string r = *iter;
            std::vector<std::string> results;
            boost::split(results, r, boost::is_any_of(":"));
            boost::algorithm::trim_left_if(results[0], boost::is_any_of("%{"));
            boost::algorithm::trim_right_if(results[(int)results.size()-1], boost::is_any_of("}"));
            q->push_back(results[(int)results.size()-1]);
    }
    return;
    */
    for (; iter != end; ++iter) {
            std::string r = *iter;
            std::vector<std::string> results;
            boost::split(results, r, boost::is_any_of(":"));
            boost::algorithm::trim_left_if(results[0], boost::is_any_of("%{"));
            boost::algorithm::trim_right_if(results[(int)results.size()-1], boost::is_any_of("}"));
            if (results.size() == 1) {
                const char *regexp = NULL;
                size_t len = 0;
                grok_pattern_find(base_, results[0].c_str(), results[0].length(), &regexp, &len);
                if (regexp != NULL) { 
                    _pattern_name_enlist(std::string(regexp), q);
                }
            }
            else if (results.size() == 2) {
                q->push_back(results[1]);
           }
    }
    return;
}

bool GrokParser::syntax_del(std::string s) {
    std::map<std::string, std::pair<grok_t, std::list<std::string> > >::iterator it = grok_list_.find(s);
    if (it == grok_list_.end()) {
        std::cout<< "[syntax_del:" << __LINE__ << "] > Failed to delete. Grok instance with provided syntax does not exist." << std::endl;
        return false;
    }
    grok_free_clone(&(it->second.first));
    grok_list_.erase(it);
    return true;
}

bool GrokParser::match(std::string strin) {
    bool m = false;
    for (std::map<std::string, std::pair<grok_t, std::list<std::string> > >::iterator it = grok_list_.begin(); it != grok_list_.end(); it++) {
        grok_match_t gm;
        if (grok_exec(&(it->second.first), strin.c_str(), &gm) == GROK_OK) {
            const char *match;
            int len(0);
            std::cout << "Message Type: " << it->first << std::endl;
            for (std::list<std::string>::iterator itt = it->second.second.begin(); 
                     itt != it->second.second.end(); itt++) {
                grok_match_get_named_substring(&gm, (*itt).c_str(), &match, &len);
                std::cout << *itt << " ==> " << std::string(match).substr(0,len) << std::endl;
            }
            if (break_if_match_) return true;
            m = true;
        }
    }
    return m;
}

void GrokParser::list_grok() {
    for (std::map<std::string, std::pair<grok_t, std::list<std::string> > >::iterator it = grok_list_.begin(); it != grok_list_.end(); it++) {
        std::cout << it->first << std::endl;
    }
}
