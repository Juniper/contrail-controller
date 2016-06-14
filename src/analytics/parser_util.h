/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */
#ifndef __PARSER_UTIL__
#define __PARSER_UTIL__
#include <string>
#include <set>
#include <map>
#include <boost/regex.hpp>
#include <pugixml/pugixml.hpp>

class LineParser
{
public:

    typedef std::set<std::string>  WordListType;

    static bool Parse(std::string s, WordListType *words);
    static bool ParseXML(const pugi::xml_node &node, WordListType *w,
            bool check_attr=true);
    static std::string GetXmlString(const pugi::xml_node node);
    static std::string MakeSane(const std::string &text);
    static unsigned int SearchPattern(const boost::regex &exp,
            std::string text);
    static unsigned int SearchPattern(std::string exp, std::string text) {
        return SearchPattern(boost::regex(exp, boost::regex::icase), text); }
private:
    template <typename Iterator>
    static bool ParseDoc(Iterator start, Iterator end,
            LineParser::WordListType *pv);
    static bool Traverse(const pugi::xml_node &node, WordListType *words,
            bool check_attr=true);
    static bool GetAtrributes(const pugi::xml_node &node, WordListType *words);
};

#endif //__PARSER_UTIL__
