/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */
#include <boost/config/warning_disable.hpp>
#include <boost/spirit/include/qi.hpp>
#include <boost/spirit/include/phoenix_core.hpp>
#include <boost/spirit/include/phoenix_operator.hpp>
#include <boost/spirit/include/phoenix_stl.hpp>

#include <boost/assign/list_of.hpp>

#include <iostream>
#include "parser_util.h"

namespace qi = boost::spirit::qi;
namespace ascii = boost::spirit::ascii;
namespace phoenix = boost::phoenix;

template <typename Iterator>
LineParser::WordListType
LineParser::ParseDoc(Iterator start, Iterator end) {
    using ascii::space;
    using qi::char_;
    using qi::double_;
    using qi::int_;
    using qi::lit;
    using qi::_1;
    using qi::lexeme;
    using qi::debug;
    using qi::on_error;
    using qi::eps;
    using qi::fail;
    using phoenix::push_back;
    using phoenix::ref;

    qi::rule<Iterator, std::string(), ascii::space_type> num =
        lexeme[ +(char_(L'0', L'1')) ];
    qi::rule<Iterator, std::string(), ascii::space_type> word =
        lexeme[ +(char_ - ' ' - ':') ];
    qi::rule<Iterator, std::string(), ascii::space_type> word2 =
        '\'' >> lexeme[ +(char_ - '\'') ] >> '\'';
    qi::rule<Iterator, std::string(), ascii::space_type> word3 =
        '"' >> lexeme[ +(char_ - '"') ] >> '"';
    qi::rule<Iterator, std::string(), ascii::space_type> word4 =
        '(' >> lexeme[ +(char_ - ')') ] >> ')';
    qi::rule<Iterator, std::string(), ascii::space_type> word5 =
        '{' >> lexeme[ +(char_ - '}') ] >> '}';
    qi::rule<Iterator, std::string(), ascii::space_type> word6 =
        '[' >> lexeme[ +(char_ - ']') ] >> ']';
    qi::rule<Iterator, std::string(), ascii::space_type> ip =
    lexeme[ +char_(L'0', L'9') >> char_('.') >> +char_(L'0', L'9')
        >> +(char_('.') >> +char_(L'0', L'9'))
        >> -(char_('/') >> +char_(L'0', L'9'))];
    qi::rule<Iterator, std::string(), ascii::space_type> ipv6 =
    lexeme[ +char_("0-9a-fA-F") >> +(+ char_(':')
        >> +char_("0-9a-fA-F")) >> -(char_('/')
        >> +char_(L'0', L'9'))];
    qi::rule<Iterator, std::string(), ascii::space_type> stats =
    lexeme[ +char_(L'0', L'9') >> +(char_('/') >> +char_(L'0', L'9'))];

    WordListType v;

    bool r = qi::phrase_parse(start, end,
        // Begin grammer
        +( *(lit(":")) >>
         ( word2 [push_back(ref(v), _1)]
         | word3 [push_back(ref(v), _1)]
         | word4 [push_back(ref(v), _1)]
         | word5 [push_back(ref(v), _1)]
         | word6 [push_back(ref(v), _1)]
         | ip    [push_back(ref(v), _1)]
         | ipv6  [push_back(ref(v), _1)]
         | stats [push_back(ref(v), _1)]
         | double_
         | int_
         | word  [push_back(ref(v), _1)]
         )
        )
        // end grammer
        , space);
    BOOST_SPIRIT_DEBUG_NODES((word)(stats)(ipv6)(ip)(word2));
    if ((start == end) && r)
        return v;
    else
        return WordListType();
}

void
LineParser::RemoveStopWords(WordListType *v) {
    WordListType::iterator i = v->begin();
    while (i != v->end()) {
    if (stop_words_.find(*i) == stop_words_.end())
        i++;
    else
        i = v->erase(i);
    }
}

std::string
LineParser::GetXmlString(const pugi::xml_node node) {
    std::ostringstream sstream;
    if (node.attribute("type").value() == std::string("string"))
        sstream <<  " " << node.child_value();
    for (pugi::xml_node child = node.first_child(); child; child =
            child.next_sibling())
        sstream << LineParser::GetXmlString(child);
    return sstream.str();
}

std::map<std::string, bool> LineParser::stop_words_ =
    boost::assign::map_list_of("via", true)("or", true)("of", true)("-", true)
        ("----", true)("the", true)("that", true);

// generate a method for the template(required for static template function
void
TemplateGen() {
    std::string s("hello");
    LineParser::WordListType words = LineParser::ParseDoc(s.begin(), s.end());
    std::cout << "result length: " << words.size() << std::endl;
}

