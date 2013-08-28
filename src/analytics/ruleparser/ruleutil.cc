/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <cassert>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <string.h>
#include <algorithm>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <limits.h>

// Careful: must include globals first for extern definitions
#include "ruleparserl.h"
#include "ruleutil.h"
#include "t_ruleparser.h"

std::string t_ruleaction::RuleActionEchoResult;

/**
 * Globals
 */
t_rulelist* g_rulelist;

/**
 * Parsing pass
 */
PARSE_MODE g_parse_mode;

/**
 * Current directory of file being parsed
 */
std::string g_curdir;

/**
 * Current file being parsed
 */
std::string g_curpath;

/**
 * Search path for inclusions
 */
std::vector<std::string> g_incl_searchpath;

/**
 * Global debug state
 */
int g_debug = 0;

/**
 * Strictness level
 */
int g_strict = 127;

/**
 * Warning level
 */
int g_warn = 1;

/**
 * Verbose output
 */
int g_verbose = 0;

/**
 * The last parsed doctext comment.
 */
char* g_doctext;

/**
 * The location of the last parsed doctext comment.
 */
int g_doctext_lineno;

/**
 * Converts a string filename into a thrift program name
 */
std::string program_name(std::string filename) {
    std::string::size_type slash = filename.rfind("/");
  if (slash != std::string::npos) {
    filename = filename.substr(slash+1);
  }
  std::string::size_type dot = filename.rfind(".");
  if (dot != std::string::npos) {
    filename = filename.substr(0, dot);
  }
  return filename;
}

/**
 * MinGW doesn't have realpath, so use fallback implementation in that case,
 * otherwise this just calls through to realpath
 */
char *saferealpath(const char *path, char *resolved_path) {
    return realpath(path, resolved_path);
}


/**
 * Report an error to the user. This is called yyerror for historical
 * reasons (lex and yacc expect the error reporting routine to be called
 * this). Call this function to report any errors to the user.
 * yyerror takes printf style arguments.
 *
 * @param fmt C format string followed by additional arguments
 */
void yyerror(const char* fmt, ...) {
    va_list args;
    fprintf(stderr,
            "[ERROR:%s:%d] (last token was '%s')\n",
            g_curpath.c_str(),
            yylineno,
            yytext);

    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);

    fprintf(stderr, "\n");
}

/**
 * Prints a debug message from the parser.
 *
 * @param fmt C format string followed by additional arguments
 */
void pdebug(const char* fmt, ...) {
    if (g_debug == 0) {
        return;
    }
    va_list args;
    printf("[PARSE:%d] ", yylineno);
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    printf("\n");
}

/**
 * Prints a verbose output mode message
 *
 * @param fmt C format string followed by additional arguments
 */
void pverbose(const char* fmt, ...) {
    if (g_verbose == 0) {
        return;
    }
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
}

/**
 * Prints a warning message
 *
 * @param fmt C format string followed by additional arguments
 */
void pwarning(int level, const char* fmt, ...) {
    if (g_warn < level) {
        return;
    }
    va_list args;
    printf("[WARNING:%s:%d] ", g_curpath.c_str(), yylineno);
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    printf("\n");
}

/**
 * Prints a failure message and exits
 *
 * @param fmt C format string followed by additional arguments
 */
void failure(const char* fmt, ...) {
    va_list args;
    fprintf(stderr, "[FAILURE:%s:%d] ", g_curpath.c_str(), yylineno);
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    printf("\n");
    //exit(1);
}

/**
 * Gets the directory path of a filename
 */
std::string directory_name(std::string filename) {
    std::string::size_type slash = filename.rfind("/");
    // No slash, just use the current directory
    if (slash == std::string::npos) {
        return ".";
    }
    return filename.substr(0, slash);
}

/**
 * Finds the appropriate file path for the given filename
 */
std::string include_file(std::string filename) {
    // Absolute path? Just try that
    if (filename[0] == '/') {
        // Realpath!
        char rp[PATH_MAX];
        if (saferealpath(filename.c_str(), rp) == NULL) {
            pwarning(0, "Cannot open include file %s\n", filename.c_str());
            return std::string();
        }

        // Stat this file
        struct stat finfo;
        if (stat(rp, &finfo) == 0) {
            return rp;
        }
    } else { // relative path, start searching
        // new search path with current dir global
        std::vector<std::string> sp = g_incl_searchpath;
        sp.insert(sp.begin(), g_curdir);

        // iterate through paths
        std::vector<std::string>::iterator it;
        for (it = sp.begin(); it != sp.end(); it++) {
            std::string sfilename = *(it) + "/" + filename;

            // Realpath!
            char rp[PATH_MAX];
            if (saferealpath(sfilename.c_str(), rp) == NULL) {
                continue;
            }

            // Stat this files
            struct stat finfo;
            if (stat(rp, &finfo) == 0) {
                return rp;
            }
        }
    }

    // Uh oh
    pwarning(0, "Could not find include file %s\n", filename.c_str());
    return std::string();
}

/**
 * Clears any previously stored doctext string.
 * Also prints a warning if we are discarding information.
 */
void clear_doctext() {
    if (g_doctext != NULL) {
        pwarning(2, "Uncaptured doctext at on line %d.", g_doctext_lineno);
    }
    free(g_doctext);
    g_doctext = NULL;
}

/**
 * Cleans up text commonly found in doxygen-like comments
 *
 * Warning: if you mix tabs and spaces in a non-uniform way,
 * you will get what you deserve.
 */
char* clean_up_doctext(char* doctext) {
    // Convert to C++ string, and remove Windows's carriage returns.
    std::string docstring = doctext;
    docstring.erase(
            remove(docstring.begin(), docstring.end(), '\r'),
            docstring.end());

    // Separate into lines.
    std::vector<std::string> lines;
    std::string::size_type pos = std::string::npos;
    std::string::size_type last;
    while (true) {
        last = (pos == std::string::npos) ? 0 : pos+1;
        pos = docstring.find('\n', last);
        if (pos == std::string::npos) {
            // First bit of cleaning.  If the last line is only whitespace, drop it.
            std::string::size_type nonwhite = docstring.find_first_not_of(" \t", last);
            if (nonwhite != std::string::npos) {
                lines.push_back(docstring.substr(last));
            }
            break;
        }
        lines.push_back(docstring.substr(last, pos-last));
    }

    // A very profound docstring.
    if (lines.empty()) {
        return NULL;
    }

    // Clear leading whitespace from the first line.
    pos = lines.front().find_first_not_of(" \t");
    lines.front().erase(0, pos);

    // If every nonblank line after the first has the same number of spaces/tabs,
    // then a star, remove them.
    bool have_prefix = true;
    bool found_prefix = false;
    std::string::size_type prefix_len = 0;
    std::vector<std::string>::iterator l_iter;
    for (l_iter = lines.begin()+1; l_iter != lines.end(); ++l_iter) {
        if (l_iter->empty()) {
            continue;
        }

        pos = l_iter->find_first_not_of(" \t");
        if (!found_prefix) {
            if (pos != std::string::npos) {
                if (l_iter->at(pos) == '*') {
                    found_prefix = true;
                    prefix_len = pos;
                } else {
                    have_prefix = false;
                    break;
                }
            } else {
                // Whitespace-only line.  Truncate it.
                l_iter->clear();
            }
        } else if (l_iter->size() > pos
                && l_iter->at(pos) == '*'
                && pos == prefix_len) {
            // Business as usual.
        } else if (pos == std::string::npos) {
            // Whitespace-only line.  Let's truncate it for them.
            l_iter->clear();
        } else {
            // The pattern has been broken.
            have_prefix = false;
            break;
        }
    }

    // If our prefix survived, delete it from every line.
    if (have_prefix) {
        // Get the star too.
        prefix_len++;
        for (l_iter = lines.begin()+1; l_iter != lines.end(); ++l_iter) {
            l_iter->erase(0, prefix_len);
        }
    }

    // Now delete the minimum amount of leading whitespace from each line.
    prefix_len = std::string::npos;
    for (l_iter = lines.begin()+1; l_iter != lines.end(); ++l_iter) {
        if (l_iter->empty()) {
            continue;
        }
        pos = l_iter->find_first_not_of(" \t");
        if (pos != std::string::npos
                && (prefix_len == std::string::npos || pos < prefix_len)) {
            prefix_len = pos;
        }
    }

    // If our prefix survived, delete it from every line.
    if (prefix_len != std::string::npos) {
        for (l_iter = lines.begin()+1; l_iter != lines.end(); ++l_iter) {
            l_iter->erase(0, prefix_len);
        }
    }

    // Remove trailing whitespace from every line.
    for (l_iter = lines.begin(); l_iter != lines.end(); ++l_iter) {
        pos = l_iter->find_last_not_of(" \t");
        if (pos != std::string::npos && pos != l_iter->length()-1) {
            l_iter->erase(pos+1);
        }
    }

    // If the first line is empty, remove it.
    // Don't do this earlier because a lot of steps skip the first line.
    if (lines.front().empty()) {
        lines.erase(lines.begin());
    }

    // Now rejoin the lines and copy them back into doctext.
    docstring.clear();
    for (l_iter = lines.begin(); l_iter != lines.end(); ++l_iter) {
        docstring += *l_iter;
        docstring += '\n';
    }

    assert(docstring.length() <= strlen(doctext));
    strcpy(doctext, docstring.c_str());
    return doctext;
}

void post_parse_cleanup() {
    yylex_destroy();
}

/**
 * Parses a program
 */
void parse(t_rulelist *rulelist, std::string file) {
    yyin = fopen(file.c_str(), "r");
    if (yyin == 0) {
        failure("Could not open input file: \"%s\"", file.c_str());
    }
    pverbose("Parsing %s for types\n", file.c_str());
    yylineno = 1;
    g_parse_mode = PROGRAM;
    g_rulelist = rulelist;

    if (yyparse() != 0) {
        failure("Parser error.");
    }
    fclose(yyin);

    post_parse_cleanup();
}

/**
 * Parses a program from memory in place with no copy
 */
void parse(t_rulelist *rulelist, char *base, size_t sz) {
    if (yy_scan_buffer(base, sz) == NULL) {
        failure("yy_scan_buffer...");
    }

    yylineno = 1;
    g_parse_mode = PROGRAM;
    g_rulelist = rulelist;

    if (yyparse() != 0) {
        failure("Parser error.");
    }
    post_parse_cleanup();
}

/**
 * Parses a program from memory with copy
 */
void parse(t_rulelist *rulelist, const char *bytes, int len) {
    if (yy_scan_bytes(bytes, len) == NULL) {
        failure("yy_scan_buffer...");
    }

    yylineno = 1;
    g_parse_mode = PROGRAM;
    g_rulelist = rulelist;

    if (yyparse() != 0) {
        failure("Parser error.");
    }
    post_parse_cleanup();
}

/**
 * parse function to verify the rulefile format
 */
int check_rulebuf(const char *bytes, int len) {
    t_rulelist *rulelist = new t_rulelist;

    if (yy_scan_bytes(bytes, len) == NULL) {
        failure("yy_scan_buffer...");
        return -1;
    }

    yylineno = 1;
    g_parse_mode = PROGRAM;
    g_rulelist = rulelist;

    if (yyparse() != 0) {
        failure("Parser error.");
        return -1;
    }

    free(rulelist);
    post_parse_cleanup();

    return 0;
}
