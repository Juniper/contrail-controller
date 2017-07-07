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

class GrokParser {
    public:
        GrokParser();
        ~GrokParser();
        void grok_parser_init();
        int grok_parser_add_pattern_from_string(const char* buffer);
        //bool grok_parser_delete_pattern(std::string name);
        bool grok_parser_match(std::string pattern, std::string strin);
        void grok_parser_free();

    private:
        void grok_match_enqueue(std::string pattern, std::queue<std::string>* q);
        void _pattern_parse_string(const char *line, const char**name, size_t *name_len,
                                   const char **regexp, size_t *regexp_len);
        grok_t grok_;
        std::map<std::string, std::string> cregex_map_;
};

#endif // __GROK_PARSER_H__
