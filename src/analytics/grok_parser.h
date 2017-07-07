extern "C" {
    #include <grok.h>
}

#include <map>
#include <string>
#include <list>

class GrokParser {

public:
    GrokParser();
    ~GrokParser();
    void init(); // Load Base Pattern
    void add_base_pattern(std::string pattern); // Add to base pattern
    bool syntax_add(std::string s); // Create grok with syntax s
    bool syntax_del(std::string s); // Delete grok with syntax s
    void match(std::string strin); // Match strin with all groks in grok_list
    void list_grok();

    bool break_if_match;
private:
    std::map<std::string, std::pair<grok_t, std::list<std::string> > > grok_list_;
    grok_t* base_;
    void _pattern_name_enlist(std::string s,
                              std::list<std::string>* q);
}; 
    
