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
#include <deque>

enum STATUS {
    IDLE,
    PROCESS,
    NOMATCH,
    MATCH
};

typedef std::map<std::string, std::pair<grok_t, std::list<std::string> > > GrokMap;
typedef std::deque<std::pair<std::string, STATUS> > MsgQueue;

class GrokParser {

public:
    GrokParser();
    ~GrokParser();
    void init(); // Load Base Pattern
    void msg_enqueue(std::string strin);
    void process_queue_wrapper();
    void process_queue();
    void add_base_pattern(std::string pattern); // Add to base pattern
    bool syntax_add(std::string s); // Create grok with syntax s
    bool syntax_del(std::string s); // Delete grok with syntax s
    void match_wrapper(std::string strin); 
    std::string match(std::string strin); // Match strin with all groks in grok_list
    void list_grok();
    void process_stop();
 
private:
    GrokMap grok_list_;
    grok_t* base_;
    MsgQueue msg_queue_;
    void _pattern_name_enlist(std::string s,
                              std::list<std::string>* q);
};

#endif // __GROKPARSER_H__
