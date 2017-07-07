/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */
#ifndef __GROK_PARSER_H__
#define __GROK_PARSER_H__

extern "C" {
    #include <grok.h>
}

#include <string>
#include <queue>
#include <map>

enum PATTERN_TYPE {
    PHRASE,
    MSG
};

class GrokParser {
    public:
        GrokParser();
        ~GrokParser();
        void parser_init();
        void add_pattern_from_string(const char* buffer, PATTERN_TYPE t);
        //bool grok_parser_delete_pattern(std::string name);
        bool msg_match(std::string strin);
        int phrase_match(std::string pattern, std::string strin);
        void parser_free();
        void list_msg_type();

    private:
        void match_enqueue(std::string pattern, std::queue<std::string>* q);
        void _pattern_parse_string(const char *line, const char**name, size_t *name_len,
                                   const char **regexp, size_t *regexp_len);
        grok_t grok_;
        std::map<std::string, std::string> mregex_map_;
};

#endif // __GROK_PARSER_H__
