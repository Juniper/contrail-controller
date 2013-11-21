/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __HTPP_CURL_INCLUDE__
#define __HTPP_CURL_INCLUDE__

#include <curl/curl.h>

/* Global information, common to all connections */
typedef struct _GlobalInfo
{
  CURLM *multi;
  int still_running;
  HttpClient *client;
} GlobalInfo;

/* Information associated with a specific easy handle */
typedef struct _ConnInfo
{
  CURL *easy;
  char *url;
  char *post;
  struct curl_slist *headers;
  GlobalInfo *global;
  char error[CURL_ERROR_SIZE + 1];
  HttpConnection *connection;
} ConnInfo;

int http_get(ConnInfo *conn, GlobalInfo *g); 
void set_url(ConnInfo *conn, const char *url); 
int curl_init(HttpClient *);
ConnInfo *new_conn(HttpConnection *connection, GlobalInfo *g,
                   bool header, bool timeout);
void del_conn(HttpConnection *connection, GlobalInfo *g);
void set_header_options(ConnInfo *conn, const char *options);
void set_put_string(ConnInfo *conn, const char *post); 
int http_put(ConnInfo *conn, GlobalInfo *g); 
void timer_cb(GlobalInfo *g);

#endif /* __HTPP_CURL_INCLUDE__ */
