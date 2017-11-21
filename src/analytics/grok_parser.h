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
#include "analytics_types.h"

typedef std::map<std::string, grok_t> GrokMap;

/*A typical GrokParser sequence is:
  create_new_grok,
  add_pattern_with_string,
  compile pattern
*/
class GrokParser {

public:
    GrokParser();
    ~GrokParser();

    /* Load Base Pattern */
    void init();

    /*create a new copy of base_ instance*/
    bool create_new_grok(std::string name);

    /*delete a grok instanc*/
    void delete_grok(std::string name);

    /* Add to pattern suit*/
    void add_pattern_from_string(std::string name, std::string full_pattern);

    /*add pattern to existed instance*/
    bool compile_pattern(std::string name);

    /* get pattern string according to name*/
    std::string get_pattern(std::string name);

    /* Match strin with all groks in grok_list */
    bool match(std::string name, std::string strin, std::map<std::string, std::string>* m);

    /* Set/Unset named_capture boolean property */
    void set_named_capture_only(bool b);

private:
    GrokMap grok_list_;
    grok_t* base_;
    bool named_capture_only_;
};

#endif // SRC_ANALYTICS_GROKPARSER_H_ 
