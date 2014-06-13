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
    static bool ParseDoc(Iterator start, Iterator end, WordListType &v);
    static void RemoveStopWrods(WordListType &v);
    static std::string GetXmlString(const pugi::xml_node node);
    private:
    static std::map<std::string, bool> stop_words_;
};

#endif //__PARSER_UTIL__
