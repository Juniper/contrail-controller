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
    break_if_match = false;
}     

void GrokParser::add_base_pattern(std::string pattern) {
    grok_patterns_import_from_string(base_, pattern.c_str());
}

/* syntax_del()
*  Create a new grok instance with defined syntax and add to map
*  @Input - s: Syntax (w/ subname) only
*  @Return - true: created
*          - false: non-exist pattern or invalid pattern
*/
bool GrokParser::syntax_add(std::string s) {
    std::vector<std::string> n;
    boost::split(n, s, boost::is_any_of(":"));
    const char *regexp = NULL;
    size_t len = 0;
    /* find syntax in base pattern */
    grok_pattern_find(base_, n[0].c_str(), n[0].length(), &regexp, &len);
    if (regexp == NULL) {
        std::cout << "[add_syntax] > Failed to create grok instance. Syntax has not defined in base_pattern." << std::endl;
        return false;
    }
    
    /* Assign grok->patterns to the subtree */
    grok_t grok;
    grok_clone(&grok, base_);
    s = "%{" + s + "}";
    /* try compile */
    if (GROK_OK != grok_compile(&grok, s.c_str())) {
        grok_free_clone(&grok);
        return NULL;
    }
    std::list<std::string> q;
    _pattern_name_enlist(std::string(regexp), &q);
    /* n.size() can only be 2 or 1
     * n.size() == 2 : contains subname
     * n.size() == 1 : contains syntax only */
    grok_list_[n[(int)n.size()-1]] = std::make_pair(grok, q);
    return true;
}

void GrokParser::_pattern_name_enlist(std::string s,
                              std::list<std::string>* q) {
    // regex
    boost::regex ex("%\\{(\\w+):(\\w+)\\}");
    boost::smatch match;
    boost::sregex_token_iterator iter(s.begin(), s.end(), ex, 0);
    boost::sregex_token_iterator end;
    for (; iter != end; ++iter) {
            std::string r = *iter;
            std::vector<std::string> results;
            boost::split(results, r, boost::is_any_of(":"));
            boost::algorithm::trim_left_if(results[0], boost::is_any_of("%{"));
            boost::algorithm::trim_right_if(results[(int)results.size()-1], boost::is_any_of("}"));
            q->push_back(results[(int)results.size()-1]);
    }
    return;
}

bool GrokParser::syntax_del(std::string s) {
    std::map<std::string, std::pair<grok_t, std::list<std::string> > >::iterator it = grok_list_.find(s);
    if (it == grok_list_.end()) {
        std::cout<< "[del_syntax] > Failed to delete. Grok instance with provided syntax does not exist." << std::endl;
        return false;
    }
    grok_free_clone(&(it->second.first));
    grok_list_.erase(it);
    return true;
}

void GrokParser::match(std::string strin) {
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
            if (break_if_match) return;
        }
    }
    return;
}

void GrokParser::list_grok() {
    for (std::map<std::string, std::pair<grok_t, std::list<std::string> > >::iterator it = grok_list_.begin(); it != grok_list_.end(); it++) {
        std::cout << it->first << std::endl;
    }
}
