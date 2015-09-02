/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

%{

#define __STDC_LIMIT_MACROS
#define __STDC_FORMAT_MACROS
#include <stdio.h>
#include <inttypes.h>
#include <limits.h>
#include "ruleparserl.h"
#include "ruleutil.h"
#include "t_ruleparser.h"
#include "ruleglob.h"

/**
 * This global variable is used for automatic numbering of field indices etc.
 * when parsing the members of a struct. Field values are automatically
 * assigned starting from -1 and working their way down.
 */
int y_field_val = -1;
int g_arglist = 0;
const int struct_is_struct = 0;
const int struct_is_union = 1;

%}

/**
 * This structure is used by the parser to hold the data types associated with
 * various parse nodes.
 */
%union {
  char            symbol;
  char*           id;
  char*           dtext;
  t_rulelist*     trulelist;
  t_rule*         trule;
  t_rulemsgtype*  tmsgtype;
  char*           tmsgtypebase;;
  char*           tcontext;
  t_rulecondlist* tcondlist;
  t_cond_base*    tcondbase;
  t_rangevalue*   trangevalue_v;
  t_rangevalue_base* trangevalue;
  t_ruleactionlist* tactionlist;
  t_ruleaction*   taction;
  t_ruleaction*   tparamlist;
}

/**
 * Strings identifier
 */
%token<id>     tok_st_identifier
%token<symbol> tok_symbolcmp

/**
 * Constant values
 */
%token<iconst> tok_int_constant
%token<dconst> tok_dub_constant

/**
 * Header keywords
 */
%token tok_rule
%token tok_include
%token tok_for
%token tok_eq
%token tok_and
%token tok_match
%token tok_in
%token tok_action
%token tok_msgtype
%token tok_context

/**
 * Grammar nodes
 */
%type<dtext>          CaptureDocText
%type<trulelist>      Rulelist
%type<trule>          Ruleitembase
%type<trule>          Ruleitem
%type<trule>          Rule
%type<tmsgtypebase>   Rulemsgtypebase
%type<tmsgtype>       Rulemsgtype
%type<tcontext>       Rulecontextbase
%type<tcontext>       Rulecontext
%type<tcondlist>      Rulecondlistbase
%type<tcondlist>      Rulecondlist
%type<tcondbase>      Ruleconditem
%type<trangevalue_v>  Rulefieldrange
%type<trangevalue>    Rulerangeitem
%type<tactionlist>    Ruleactionlist
%type<taction>        Ruleaction
%type<taction>        Actionparamlist

%%

/**
 * Rule Grammar Implementation.
 *
 */
Rulefile:
    Rulelist
    {
        pdebug("Rulefile -> Rulelist");
    }

Rulelist:
    Ruleitembase
    {
        pdebug("Rulelist -> Ruleitembase");
        g_rulelist->add_rule($1);
        $$ = g_rulelist;
    }
| Rulelist Ruleitembase
    {
        pdebug("Rulelist -> Rulelist Ruleitembase");
        g_rulelist->add_rule($2);
        $$ = g_rulelist;
    }

CaptureDocText:
    {
        pdebug("CaptureDocText ->");
        if (g_parse_mode == PROGRAM) {
          $$ = g_doctext;
          g_doctext = NULL;
        } else {
          $$ = NULL;
        }
    }

Ruleitembase:
CaptureDocText Ruleitem
    {
        pdebug("Ruleitembase -> CaptureDocText Ruleitem");
        pdebug("CaptureDocText is %p, Ruleitem is %p", $1, $2);
        if ($1 != NULL) {
            $2->set_doc($1);
        }
        $$ = $2;
    }

Ruleitem:
tok_rule tok_st_identifier ':' Rule
    {
        pdebug("Ruleitem -> tok_rule tok_st_identifier...");
        t_rule* ruleptr = $4;

        ruleptr->set_name($2);
        free($2);
        $$ = $4;
    }

Rule:
  tok_for Rulemsgtype Rulecondlistbase Ruleactionlist
    {
      pdebug("Rule -> tok_for Rulemsgtype Rulecondlistbase Ruleactionlist");
      t_rule *rule = new t_rule($2, $3, $4);
      $$ = rule;
    }

Rulemsgtype:
Rulemsgtypebase Rulecontext
    {
      pdebug("Rulemsgtype -> Rulemsgtypebase Rulecontext");
      t_rulemsgtype *msgtype;
      if ($2 == NULL) {
          msgtype = new t_rulemsgtype($1);
          free($1);
      } else {
          msgtype = new t_rulemsgtype($1, $2);
          free($1);
          free($2);
      }
      $$ = msgtype;
    }
| '(' Rulemsgtype ')'
    {
      pdebug("Rulemsgtype -> ( Rulemsgtype )");
      $$ = $2;
    }

Rulemsgtypebase:
tok_msgtype tok_eq tok_st_identifier
    {
      pdebug("Rulemsgtypebase -> tok_msgtype tok_eq tok_st_identifier");
      $$ = $3;
    }
| '(' Rulemsgtypebase ')'
    {
      pdebug("Rulemsgtypebase -> ( Rulemsgtypebase )");
      $$ = $2;
    }

Rulecontext:
    {
      pdebug("Rulecontext -> ");
      $$ = NULL;
    }
| tok_and Rulecontextbase
    {
      pdebug("Rulecontext -> tok_and Rulecontextbase");
      $$ = $2;
    }

Rulecontextbase:
tok_context tok_eq tok_st_identifier
    {
      pdebug("Rulecontextbase -> tok_context tok_eq tok_st_identifier");
      $$ = $3;
    }
| '(' Rulecontextbase ')'
    {
      pdebug("Rulecontextbase -> ( Rulecontextbase )");
      $$ = $2;
    }

Rulecondlistbase:
    {
      pdebug("Rulecondlistbase -> ");
      $$ = NULL;
    }
| tok_match Rulecondlist
    {
      pdebug("Rulecondlistbase -> tok_match Rulecondlist");
      $$ = $2;
    }

Rulecondlist:
  Ruleconditem
    {
      pdebug("Rulecondlist -> Ruleconditem");
      t_rulecondlist *condlist = new t_rulecondlist();

      condlist->add_field($1);
      $$ = condlist;
    }
| Ruleconditem tok_and Rulecondlist
    {
      pdebug("Rulecondlist -> Ruleconditem tok_and Rulecondlist");
      t_rulecondlist *condlist = $3;
      condlist->add_field($1);
      $$ = condlist;
    }

Ruleconditem:
  tok_st_identifier tok_symbolcmp tok_st_identifier
    {
      pdebug("Ruleconditem -> tok_st_identifier tok_symbolcmp tok_st_identifier");
      t_cond_simple *cond =
          new t_cond_simple($1, $2, $3);
      free($1);
      free($3);
      $$ = cond;
    }
| tok_st_identifier tok_in '[' Rulefieldrange ']'
    {
      pdebug("Ruleconditem -> tok_st_identifier tok_in [ Rulefieldrange ]");
      t_cond_range *cond = new t_cond_range($1, $4);
      free($1);
      $$ = cond;
    }
| '(' Ruleconditem ')'
    {
      pdebug("Ruleconditem -> ( Ruleconditem ) ");
      $$ = $2;
    }

Rulefieldrange:
  Rulerangeitem
    {
      pdebug("Rulefieldrange -> Rulerangeitem");
      t_rangevalue* rangevalue = new t_rangevalue();

      rangevalue->add_rangevalue($1);
      $$ = rangevalue;
    }
| Rulefieldrange ',' Rulerangeitem
    {
      pdebug("Rulefieldrange -> Rulefieldrange , Rulerangeitem");
      t_rangevalue* rangevalue = $1;
      rangevalue->add_rangevalue($3);
      $$ = rangevalue;
    }

Rulerangeitem:
  tok_st_identifier
    {
      pdebug("Rulerangeitem -> tok_st_identifier");
      t_rangevalue_s* rangevalue_s = new t_rangevalue_s($1);
      free($1);
      $$ = rangevalue_s;
    }
| tok_st_identifier '-' tok_st_identifier
    {
      pdebug("Rulerangeitem -> tok_st_identifier - tok_st_identifier");
      t_rangevalue_d *rangevalue_d = new t_rangevalue_d($1, $3);
      free($1);
      free($3);
      $$ = rangevalue_d;
    }

Ruleactionlist:
Ruleaction
    {
      pdebug("Ruleactionlist -> Ruleaction");
      t_ruleactionlist *actionlist = new t_ruleactionlist();

      actionlist->add_action($1);
      $$ = actionlist;
    }
| Ruleaction Ruleactionlist
    {
      pdebug("Ruleactionlist -> Ruleaction Ruleactionlist");
      t_ruleactionlist *actionlist = $2;
      actionlist->add_action($1);

      $$ = actionlist;
    }

Ruleaction: tok_action tok_st_identifier Actionparamlist 
    {
      pdebug("Ruleaction -> tok_action tok_st_identifier Actionparamlist");
      $3->set_actionid($2);
      free($2);
      $$ = $3;
    }

Actionparamlist:
tok_st_identifier
    {
      pdebug("Actionparamlist -> tok_st_identifier");
      t_ruleaction *action = new t_ruleaction();

      action->add_actionparam($1);
      free($1);
      $$ = action;
    }
| Actionparamlist tok_st_identifier
    {
      pdebug("Actionparamlist -> Actionparamlist tok_st_identifier");
      $1->add_actionparam($2);
      free($2);
      $$ = $1;
    }

%%
