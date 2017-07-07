/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __GROKPARSER_H__
#define __GROKPARSER_H__

extern "C" {
    #include <grok.h>
}
#include <map>
#include <string>
#include <list>

typedef std::map<std::string, std::pair<grok_t, std::list<std::string> > > GrokMap;

class GrokParser {

public:
    GrokParser();
    ~GrokParser();
    void init(); // Load Base Pattern
    void add_base_pattern(std::string pattern); // Add to base pattern
    bool syntax_add(std::string s); // Create grok with syntax s
    bool syntax_del(std::string s); // Delete grok with syntax s
    std::string match(std::string strin); // Match strin with all groks in grok_list
    void list_grok();
    void set_break_if_match(bool b);
private:
    GrokMap grok_list_;
    grok_t* base_;
    void _pattern_name_enlist(std::string s,
                              std::list<std::string>* q);
    bool break_if_match_;

};

#endif // __GROKPARSER_H__
