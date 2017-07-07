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
    bool match(std::string strin, std::map<std::string, std::string>* m);

    /* Set/Unset named_capture boolean property */
    void set_named_capture_only(bool b);

    /* Create and send GenericStats Objectlog */
    void send_generic_stat(std::map<std::string, std::string> &m_in);

    /* Set key list for GenericStats */
    void set_key_list(const std::vector<std::string> &key);

    /* Set attrib list for GenericStats */
    void set_attrib_list(const std::vector<std::string> &attrib);

private:
    GrokMap grok_list_;
    grok_t* base_;
    bool named_capture_only_;
    std::vector<std::string> keys_;
    std::vector<std::string> attribs_;
};

#endif // SRC_ANALYTICS_GROKPARSER_H_ 
