
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
#include <vector>

typedef std::map<std::string, grok_t> GrokMap;

class GrokParser {

public:
    GrokParser();
    ~GrokParser();

    /* Load Base Pattern */
    void init();

    /* Add to base pattern */
    void add_base_pattern(std::string pattern);

    /* Create grok with syntax s */
    bool msg_type_add(std::string s);

    /* Delete grok with syntax s */
    bool msg_type_del(std::string s);

    /* Match strin with all groks in grok_list */
    bool match(std::string name, std::string strin, std::map<std::string, std::string>* m);

    /* Set/Unset named_capture boolean property */
    void set_named_capture_only(bool b);

     /* get pattern string according to name*/
    std::string get_base_pattern(std::string name);
private:
    GrokMap grok_list_;
    grok_t* base_;
    bool named_capture_only_;
};

#endif // SRC_ANALYTICS_GROKPARSER_H_ 
