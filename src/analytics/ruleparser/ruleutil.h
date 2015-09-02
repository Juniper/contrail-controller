/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef T_RULEUTIL_H
#define T_RULEUTIL_H

#include <string>

typedef enum {
    INCLUDES = 1,
    PROGRAM,
} PARSE_MODE;

int yyparse(void);

/**
 * Expected to be defined by Flex/Bison
 */
void yyerror(const char* fmt, ...);

/**
 * Parse debugging output, used to print helpful info
 */
void pdebug(const char* fmt, ...);

/**
 * Parser warning
 */
void pwarning(int level, const char* fmt, ...);

/**
 * Failure!
 */
void failure(const char* fmt, ...);

/**
 * Converts a string filename into a thrift program name
 */
std::string program_name(std::string filename);

/**
 * Gets the directory path of a filename
 */
std::string directory_name(std::string filename);

/**
 * Get the absolute path for an include file
 */
std::string include_file(std::string filename);

char *saferealpath(const char *path, char *resolved_path);

/**
 * Clears any previously stored doctext string.
 */
void clear_doctext();

/**
 * Cleans up text commonly found in doxygen-like comments
 */
char* clean_up_doctext(char* doctext);

#endif
