/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

%{

#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-label"

#include <string>
#include <errno.h>

#include "ruleutil.h"
#include "t_ruleparser.h"
#include "ruleglob.h"

#include "ruleparsery.hh"

void rule_eng_reserved_keyword(char* keyword) {
  yyerror("Cannot use reserved language keyword: \"%s\"\n", keyword);
  exit(1);
}

void integer_overflow(char* text) {
  yyerror("This integer is too big: \"%s\"\n", text);
  exit(1);
}

void unexpected_token(char* text) {
  yyerror("Unexpected token in input: \"%s\"\n", text);
  exit(1);
}

%}

/**
 * Provides the yylineno global, useful for debugging output
 */
%option lex-compat

/**
 * Our inputs are all single files, so no need for yywrap
 */
%option noyywrap

/**
 * We don't use it, and it fires up warnings at -Wall
 */
%option nounput

/**
 * Helper definitions, comments, constants, and whatnot
 */

st_identifier    ([a-zA-Z_0-9][\.a-zA-Z_0-9]*)
whitespace    ([ \t\r\n]*)
sillycomm     ("/*""*"*"*/")
multicomm     ("/*"[^*]"/"*([^*/]|[^*]"/"|"*"[^/])*"*"*"*/")
doctext       ("/**"([^*/]|[^*]"/"|"*"[^/])*"*"*"*/")
comment       ("//"[^\n]*)
unixcomment   ("#"[^\n]*)
symbol        ([:;\-\,\{\}\(\)\[\]])
symbolcmp     ([=<>|])
literal_begin (['\"])

%%

{whitespace}         { /* do nothing */                 }
{sillycomm}          { /* do nothing */                 }
{multicomm}          { /* do nothing */                 }
{comment}            { /* do nothing */                 }
{unixcomment}        { /* do nothing */                 }

{symbolcmp} {
  yylval.symbol = yytext[0];
  return tok_symbolcmp;
}

{symbol}             { return yytext[0];                }
"*"                  { return yytext[0];                }

"include"            { return tok_include;              }
"Rule"               { return tok_rule;                 }
"For"                { return tok_for;                  }
"eq"                 { return tok_eq;                   }
"match"              { return tok_match;                }
"and"                { return tok_and;                  }
"action"             { return tok_action;               }
"in"                 { return tok_in;                   }
"msgtype"            { return tok_msgtype;              }
"context"            { return tok_context;              }

{st_identifier} {
  yylval.id = strdup(yytext);
  return tok_st_identifier;
}

{doctext} {
 /* This does not show up in the parse tree. */
 /* Rather, the parser will grab it out of the global. */
  if (g_parse_mode == PROGRAM) {
    clear_doctext();
    g_doctext = strdup(yytext + 3);
    g_doctext[strlen(g_doctext) - 2] = '\0';
    g_doctext = clean_up_doctext(g_doctext);
    g_doctext_lineno = yylineno;
  }
}

. {
  unexpected_token(yytext);
}


. {
  /* Catch-all to let us catch "*" in the parser. */
  return (int) yytext[0];
}

%%

