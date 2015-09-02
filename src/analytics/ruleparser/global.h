/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef RULEGLOB_H
#define RULEGLOB_H

/**
 * Globals
 */
extern t_rulelist* g_rulelist;
extern PARSE_MODE g_parse_mode;
extern char* g_doctext;
extern int g_doctext_lineno;
void parse(t_rulelist *rulelist, std::string file);

#endif //RULEGLOB_H
