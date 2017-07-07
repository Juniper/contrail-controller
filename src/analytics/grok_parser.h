extern "C" {
    #include <grok.h>
}

#include <map>
#include <string>

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
private:
    std::map<std::string, grok_t> grok_list_;
    grok_t* base_;

}; 
    
