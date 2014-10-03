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
  uint32_t post_len;
  struct curl_slist *headers;
  GlobalInfo *global;
  char error[CURL_ERROR_SIZE + 1];
  HttpConnection *connection;
} ConnInfo;

class CurlErrorCategory : public boost::system::error_category
{
 public:
    virtual const char *name() const { return "http_curl"; }
    virtual std::string message( int ev ) const {
        return curl_easy_strerror((CURLcode)ev);
    }
};
extern const CurlErrorCategory curl_error_category;

int http_get(ConnInfo *conn, GlobalInfo *g); 
void set_url(ConnInfo *conn, const char *url); 
int curl_init(HttpClient *);
ConnInfo *new_conn(HttpConnection *connection, GlobalInfo *g,
                   bool header, bool timeout);
void del_conn(HttpConnection *connection, GlobalInfo *g);
void set_header_options(ConnInfo *conn, const char *options);
void set_post_string(ConnInfo *conn, const char *post, uint32_t len);
void set_put_string(ConnInfo *conn, const char *put, uint32_t len);
int http_head(ConnInfo *conn, GlobalInfo *g); 
int http_put(ConnInfo *conn, GlobalInfo *g);
int http_post(ConnInfo *conn, GlobalInfo *g);
int http_delete(ConnInfo *conn, GlobalInfo *g);
bool timer_cb(GlobalInfo *g);

#endif /* __HTPP_CURL_INCLUDE__ */
