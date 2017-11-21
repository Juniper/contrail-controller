
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

typedef std::map<std::string, grok_t*> GrokMap;

class GrokParser {

public:
    GrokParser();
    ~GrokParser();

    /* Load Base Pattern */
    void init();
    /*create grok instance*/
    void create_grok_instance(std::string name);

    /* Add pattern to grok instance*/
    void add_pattern(std::string name, std::string pattern);

    /* compile grok instance pattern */
    bool compile_pattern(std::string name);

    /* Delete grok instance */
    bool del_grok_instance(std::string name);

    /* Match strin with all groks in grok_list */
    bool match(std::string name, std::string strin, std::map<std::string, std::string>* m);

    /* Set/Unset named_capture boolean property */
    void set_named_capture_only(bool b);

    /*Get pre install pattern*/
    std::vector<std::string> get_base_pattern();
private:
    GrokMap grok_list_;
    bool named_capture_only_;
    void install_base_pattern(std::string name);
    void init_base_pattern();
};

#endif // SRC_ANALYTICS_GROKPARSER_H_ 
