/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */
#define BOOST_SPIRIT_DEBUG
#include "boost/spirit/include/classic.hpp"
#include <boost/config/warning_disable.hpp>
#include <boost/spirit/include/qi.hpp>
#include <boost/spirit/include/phoenix_core.hpp>
#include <boost/spirit/include/phoenix_operator.hpp>
#include <boost/spirit/include/phoenix_stl.hpp>
#include <boost/spirit/include/qi_repeat.hpp>

#include <boost/assign/list_of.hpp>

#include <boost/algorithm/string/case_conv.hpp>

#include <iostream>
#include "parser_util.h"

namespace qi = boost::spirit::qi;
namespace ascii = boost::spirit::ascii;
namespace phx = boost::phoenix;
using namespace BOOST_SPIRIT_CLASSIC_NS;



bool
LineParser::GetAtrributes(const pugi::xml_node &node,
        LineParser::WordListType *words)
{
     bool r=true;
     for (pugi::xml_attribute attr = node.first_attribute(); attr;
             attr = attr.next_attribute()) {
         std::string s =  boost::algorithm::to_lower_copy(std::string(
                     attr.value()));
         if (!s.empty()) {
             r &= ParseDoc(s.begin(), s.end(), words);
         }
     }
     return r;
}

bool
LineParser::Traverse(const pugi::xml_node &node,
        LineParser::WordListType *words, bool check_attr)
{
    pugi::xml_node_type type = node.type();
    bool r = true;

    if (type == pugi::node_element) {
        if (check_attr)
            r &= GetAtrributes(node, words);
    } else if (type == pugi::node_pcdata || type == pugi::node_cdata) {
         std::string s =  boost::algorithm::to_lower_copy(std::string(
                     node.value()));
         if (!s.empty()) {
             r &= ParseDoc(s.begin(), s.end(), words);
         }
    }
    for (pugi::xml_node s = node.first_child(); s; s = s.next_sibling())
        r &= Traverse(s, words, check_attr);
    return r;
}

bool
LineParser::ParseXML(const pugi::xml_node &node,
        LineParser::WordListType *words, bool check_attr)
{
    bool r=true;
    if (check_attr)
        r &= GetAtrributes(node, words);
    for (pugi::xml_node s = node; s; s = s.next_sibling())
        r &= Traverse(s, words, check_attr);
    return r;
}

bool
LineParser::Parse(std::string s, LineParser::WordListType *words) {
    std::string ls = boost::algorithm::to_lower_copy(s);
    return ParseDoc(ls.begin(), ls.end(), words);
}

template<typename Iterator>
struct msg_skipper : public qi::grammar<Iterator> {
    msg_skipper() : msg_skipper::base_type(skip, "msgskpr") {
        skip = ascii::space | qi::char_(".,;:[](){}\t\r");
    }
    qi::rule<Iterator> skip;
};

template <typename Iterator>
bool
LineParser::ParseDoc(Iterator start, Iterator end,
        LineParser::WordListType *pv)
{
    using ascii::space;
    using qi::char_;
    using qi::lit;
    using qi::_1;
    using qi::lexeme;
    using qi::debug;
    using phx::insert;
    using phx::ref;
    using boost::spirit::repeat;

    typedef msg_skipper<Iterator> skipper_t;
    skipper_t skpr;

    qi::rule<Iterator, std::string(), skipper_t> num1 =
        lexeme[ *char_("0-9") >> '.' >> +char_("0-9") ];
    qi::rule<Iterator, std::string(), skipper_t> num2=
        lexeme[ +char_("0-9") >> -lit('.') ];
    qi::rule<Iterator, std::string(), skipper_t> hex1=
        lexeme[ lit('0') >> lit('x') >> +char_("0-9A-Fa-f") ];
    qi::rule<Iterator, std::string(), skipper_t> oct1=
        lexeme[ lit('0') >> +char_("0-7") ];
    qi::rule<Iterator, std::string(), skipper_t> word =
        lexeme[ +(char_ - char_(" .,;:[](){}\t\r")) ];
    qi::rule<Iterator, std::string(), skipper_t> word2 =
        '\'' >> lexeme[ +(char_ - '\'') ] >> '\'';
    qi::rule<Iterator, std::string(), skipper_t> word3 =
        '"' >> lexeme[ +(char_ - '"') ] >> '"';
    qi::rule<Iterator, std::string(), skipper_t> uuid =
        lexeme[ repeat(8)[char_("0-9a-fA-F")] >> char_('-') >>
         repeat(3)[ repeat(4)[char_("0-9a-fA-F")] >> char_('-') ] >>
         repeat(12)[char_("0-9a-fA-F")] ];
    qi::rule<Iterator, std::string(), skipper_t> ip =
    lexeme[ +char_(L'0', L'9') >> char_('.') >> +char_(L'0', L'9')
        >> +(char_('.') >> +char_(L'0', L'9'))
        >> -(char_('/') >> +char_(L'0', L'9'))];
    qi::rule<Iterator, std::string(), skipper_t> ipv6 =
    lexeme[ +char_("0-9a-fA-F") >> +(+ char_(':')
        >> +char_("0-9a-fA-F")) >> -(char_('/')
        >> +char_(L'0', L'9'))];
    qi::rule<Iterator, std::string(), skipper_t> stats =
    lexeme[ +char_(L'0', L'9') >> +(char_('/') >> +char_(L'0', L'9'))];

    qi::symbols<char, bool> stop_words;
    stop_words.add
        ("via", true)("or", true)("of", true)
        ("string", true)("sandesh", true)("client", true)
        ("the", true)("that", true)("and", true);
    qi::rule<Iterator, std::string(), skipper_t> aw =
        +( *( lit(":")
            | lit(",")
            | lit(".")
            | lit(";")
            | lit("&")
            ) >>
         ( stop_words
         | stats [insert(ref(*pv), _1)]
         | word2 [insert(ref(*pv), _1)]
         | word3 [insert(ref(*pv), _1)]
         | uuid  [insert(ref(*pv), _1)]
         | ip    [insert(ref(*pv), _1)]
         | ipv6  [insert(ref(*pv), _1)]
         | hex1
         | oct1
         | num1
         | num2
         | word  [insert(ref(*pv), _1)]
         )
        );

    bool r = qi::phrase_parse(start, end,
        // Begin grammer
        *aw
        // end grammer
        , skpr);
    BOOST_SPIRIT_DEBUG_NODES((word)(num1)(aw)(word2));
    BOOST_SPIRIT_DEBUG_RULE(word);
    BOOST_SPIRIT_DEBUG_RULE(word2);
    BOOST_SPIRIT_DEBUG_RULE(aw);
    return ((start == end) && r);
}

std::string
LineParser::MakeSane(const std::string &text) {
    std::ostringstream s;
    for (std::string::const_iterator it = text.begin(); it != text.end();
            ++it) {
        if (0x80 & *it)
            s << "&#" << (int)((uint8_t)*it) << ";";
        else
            s << *it;
    }
    return s.str();
}

std::string
LineParser::GetXmlString(const pugi::xml_node node) {
    std::ostringstream sstream;
    if (node.attribute("type").value() == std::string("string"))
        sstream <<  " " << LineParser::MakeSane(node.child_value());
    for (pugi::xml_node child = node.first_child(); child; child =
            child.next_sibling())
        sstream << LineParser::GetXmlString(child);
    return sstream.str();
}

unsigned int
LineParser::SearchPattern(boost::regex exp, std::string text)
{
    unsigned int cnt = 0;

    boost::match_results<std::string::const_iterator> what;
    boost::match_flag_type flags = boost::match_default;
    std::string::const_iterator start = text.begin(), end = text.end();
    while(regex_search(start, end, what, exp, flags)) {
        cnt++;
        //start looking for next match after this one
        start = what[0].second;
        // update flags:
        flags |= boost::match_prev_avail;
        flags |= boost::match_not_bob;
//#define SYSLGDEBUG
#ifdef SYSLGDEBUG
        std::cout << "Text:        \"" << text << "\"\n";
        std::cout << "<String 5> \""
                  << std::string(what[5].first, what[5].second)
                  << "\" + <string 6> \"" << std::string(what[6].first, what[6].second)
                  << "\" = " << what[5].first - text.begin();
        std::cout << "\n what.size = " << what.size()
                  << "\n====================\n";
        for(unsigned int i = 0; i < what.size(); ++i)
        {
            std::cout << "      $" << i << " = {";
            std::cout << std::string(what[i].first,  what[i].second);
            std::cout << "}\n";
        }
#endif
    }
    return cnt;
}


// generate a method for the template(required for static template function
void
TemplateGen() {
    std::string s("hello");
    LineParser::WordListType words;
    LineParser::Parse(s, &words);
    std::cout << "result length: " << words.size() << std::endl;
}

