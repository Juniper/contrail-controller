/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_ANALYTICS_GROKPARSER_H_
#define SRC_ANALYTICS_GROKPARSER_H_

extern "C" {
    #include <grok.h>
}
#include <map>
#include <string>
#include <deque>
#include <set>

typedef std::map<std::string, grok_t> GrokMap;
typedef std::map<std::string, std::pair<std::string, grok_match_t> > MatchedMsgList;

class GrokParser {

public:
    GrokParser();
    ~GrokParser();
    void init(); // Load Base Pattern
    void add_base_pattern(std::string pattern); // Add to base pattern
    bool msg_type_add(std::string s); // Create grok with syntax s
    bool msg_type_del(std::string s); // Delete grok with syntax s
    std::string match(std::string strin); // Match strin with all groks in grok_list
    void get_matched_data(std::string s, std::map<std::string, std::string>* m);
    void set_named_capture_only(bool b);

private:
    GrokMap grok_list_;
    grok_t* base_;
    MatchedMsgList matched_msg_list_;
    bool named_capture_only_;
};

#endif // SRC_ANALYTICS_GROKPARSER_H_ 
