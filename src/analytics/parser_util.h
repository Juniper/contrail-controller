/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */
#ifndef __PARSER_UTIL__
#define __PARSER_UTIL__
#include <string>
#include <vector>
#include <map>

#include <pugixml/pugixml.hpp>

class LineParser
{
public:

    typedef std::vector<std::string>  WordListType;

    template <typename Iterator>
    static WordListType ParseDoc(Iterator start, Iterator end);
    static void RemoveStopWords(WordListType *v);
    static std::string GetXmlString(const pugi::xml_node node);
    static std::string MakeSane(const std::string &text);
private:
    static std::map<std::string, bool> stop_words_;
};

#endif //__PARSER_UTIL__
