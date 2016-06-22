/***************************************************************************
 *                                  _   _ ____  _
 * Copyright (C) 2012, Daniel Stenberg, <daniel@haxx.se>, et al.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution. The terms
 * are also available at http://curl.haxx.se/docs/copyright.html.
 *
 * You may opt to use, copy, modify, merge, publish, distribute and/or sell
 * copies of the Software, and permit persons to whom the Software is
 * furnished to do so, under the terms of the COPYING file.
 *
 * This software is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY
 * KIND, either express or implied.
 *
 ***************************************************************************/

/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "http_client.h"
#include "http_curl.h"
#include <curl/curl.h>
#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/intrusive_ptr.hpp>
#include <tbb/mutex.h>

#define MSG_OUT stdout /* Send info to stdout, change to stderr if you want */

using tbb::mutex;

const CurlErrorCategory curl_error_category;

/* Update the event timer after curl_multi library calls */
static int multi_timer_cb(CURLM *multi, long timeout_ms, HttpClient *client)
{

  if ( timeout_ms > 0 )
  {
    client->StartTimer(timeout_ms);
  }
  else
  {
    client->CancelTimer();
    timer_cb(client->GlobalInfo());
  }

  return 0;
}

/* Die if we get a bad CURLMcode somewhere */
static void mcode_or_die(const char *where, CURLMcode code)
{
  if ( CURLM_OK != code )
  {
    const char *s;
    switch ( code )
    {
    case CURLM_CALL_MULTI_PERFORM: s="CURLM_CALL_MULTI_PERFORM"; break;
    case CURLM_BAD_HANDLE:         s="CURLM_BAD_HANDLE";         break;
    case CURLM_BAD_EASY_HANDLE:    s="CURLM_BAD_EASY_HANDLE";    break;
    case CURLM_OUT_OF_MEMORY:      s="CURLM_OUT_OF_MEMORY";      break;
    case CURLM_INTERNAL_ERROR:     s="CURLM_INTERNAL_ERROR";     break;
    case CURLM_UNKNOWN_OPTION:     s="CURLM_UNKNOWN_OPTION";     break;
    case CURLM_LAST:               s="CURLM_LAST";               break;
    case CURLM_BAD_SOCKET:         s="CURLM_BAD_SOCKET";         break;
    default:                       s="CURLM_unknown";            break;
    }

    fprintf(MSG_OUT, "\nERROR: %s returns %s", where, s);
  }
}

/* Check for completed transfers, and remove their easy handles */
static void check_multi_info(GlobalInfo *g)
{
  char *eff_url;
  CURLMsg *msg;
  int msgs_left;
  ConnInfo *conn;
  CURL *easy;
  CURLcode res;

  while ((msg = curl_multi_info_read(g->multi, &msgs_left)))
  {
    if (msg->msg == CURLMSG_DONE)
    {
      easy = msg->easy_handle;
      res = msg->data.result;
      curl_easy_getinfo(easy, CURLINFO_PRIVATE, &conn);
      curl_easy_getinfo(easy, CURLINFO_EFFECTIVE_URL, &eff_url);

      if (conn) {
        boost::system::error_code error(res, curl_error_category);
        std::string empty_str("");
        if (conn->connection->HttpClientCb() != NULL)
            conn->connection->HttpClientCb()(empty_str, error);
      }
    }
  }
}

typedef boost::intrusive_ptr<HttpClientSession> TcpSessionPtr;

static void event_cb_impl(GlobalInfo *g, TcpSessionPtr session, int action,
                          const boost::system::error_code &error, 
                          std::size_t bytes_transferred)
{

  if (session->IsClosed()) return;

  if (g->client->IsErrorHard(error)) return;

  CURLMcode rc;
  rc = curl_multi_socket_action(g->multi, session->socket()->native_handle(), action, &g->still_running);

  mcode_or_die("event_cb: curl_multi_socket_action", rc);
  check_multi_info(g);

  if ( g->still_running <= 0 )
  {
    g->client->CancelTimer();
  }
}

/* Called by asio when there is an action on a socket */
static void event_cb(GlobalInfo *g, TcpSessionPtr session, int action,
                     const boost::system::error_code &error, std::size_t bytes_transferred)
{
  tbb::mutex::scoped_lock lock(session->mutex());

  // Ignore if the connection is already deleted.
  if (!session->Connection()) return;

  HttpClient *client = session->Connection()->client();
  client->ProcessEvent(boost::bind(&event_cb_impl, g, session, action, error, 
                                   bytes_transferred));
}

/* Called by asio when our timeout expires */
bool timer_cb(GlobalInfo *g)
{
    CURLMcode rc;
    rc = curl_multi_socket_action(g->multi, CURL_SOCKET_TIMEOUT, 0, &g->still_running);
    mcode_or_die("timer_cb: curl_multi_socket_action", rc);

    // When timeout happens, call multi_perform to check if we still have
    // pending handles; if yes, continue running the timer
    rc = curl_multi_perform(g->multi, &g->still_running);
    mcode_or_die("timer_cb: curl_multi_perform", rc);

    check_multi_info(g);
    return (g->still_running > 0);
}

/* Clean up any data */
static void remsock(SockInfo *sock_info, GlobalInfo *g)
{
  if ( sock_info )
  {
    free(sock_info);
  }
}

static bool setsock(SockInfo *sock_info, curl_socket_t s, CURL*e, int act, GlobalInfo *g)
{
  if (!sock_info || !sock_info->conn_info || !sock_info->conn_info->connection)
  {
    return false;
  }

  HttpClientSession *session = sock_info->conn_info->connection->session();
  if (!session || session->IsClosed())
       return false;

  boost::asio::ip::tcp::socket * tcp_socket = session->socket();

  sock_info->action = act;

  if ( act == CURL_POLL_IN )
  {
    tcp_socket->async_read_some(boost::asio::null_buffers(),
                                boost::bind(&event_cb, g, TcpSessionPtr(session), 
                                act, _1, _2));
  }
  else if ( act == CURL_POLL_OUT )
  {
    tcp_socket->async_write_some(boost::asio::null_buffers(),
                                 boost::bind(&event_cb, g, TcpSessionPtr(session), 
                                 act, _1, _2));
  }
  else if ( act == CURL_POLL_INOUT )
  {
    tcp_socket->async_read_some(boost::asio::null_buffers(),
                                boost::bind(&event_cb, g, TcpSessionPtr(session),
                                act, _1, _2));
    tcp_socket->async_write_some(boost::asio::null_buffers(),
                                 boost::bind(&event_cb, g, TcpSessionPtr(session),
                                 act, _1, _2));
  }
  return true;
}


static void addsock(curl_socket_t s, CURL *easy, int action, GlobalInfo *g)
{
  // store socket details in SockInfo
  SockInfo *sock_info = (SockInfo *)calloc(sizeof(SockInfo), 1);
  curl_easy_getinfo(easy, CURLINFO_PRIVATE, &sock_info->conn_info);

  if (!setsock(sock_info, s, easy, action, g)) {
      free(sock_info);
      return;
  }

  curl_multi_assign(g->multi, s, sock_info);
}

/* CURLMOPT_SOCKETFUNCTION */
static int sock_cb(CURL *e, curl_socket_t s, int what, void *cbp, void *sockp)
{
  GlobalInfo *g = (GlobalInfo*) cbp;
  SockInfo *sock_info = (SockInfo *)sockp;

  if ( what == CURL_POLL_REMOVE )
  {
    remsock(sock_info, g);
  }
  else
  {
    if ( !sockp )
    {
      addsock(s, e, what, g);
    }
    else
    {
      setsock(sock_info, s, e, what, g);
    }
  }
  return 0;
}


/* CURLOPT_WRITEFUNCTION */
static size_t write_cb(void *ptr, size_t size, size_t nmemb, void *data)
{
  size_t written = size * nmemb;

  HttpConnection *conn = static_cast<HttpConnection *>(data);
  conn->AssignData((char *)ptr, written);
  return written;
}

/* CURLOPT_HEADERFUNCTION */
static size_t header_cb(void *ptr, size_t size, size_t nmemb, void *data)
{
  size_t written = size * nmemb;

  HttpConnection *conn = static_cast<HttpConnection *>(data);
  conn->AssignHeader((char *)ptr, written);
  return written;
}

static size_t read_cb(void *ptr, size_t size, size_t nmemb, void *data)
{
  HttpConnection *conn = static_cast<HttpConnection *>(data);
  ConnInfo *curl_handle = conn->curl_handle();
  char *str = curl_handle->post;

  size_t maxb = size*nmemb;
  size_t offset = conn->GetOffset();
  size_t datasize = 0;
  if (curl_handle->post_len > offset)
      datasize = curl_handle->post_len - offset;

  if (maxb >= datasize) {
      memcpy(ptr, str + offset, datasize);
      conn->UpdateOffset(datasize);
      return datasize;
  } else {
      memcpy(ptr, str + offset, maxb);
      conn->UpdateOffset(maxb);
      return maxb;
  }

  return 0;
}


/* CURLOPT_PROGRESSFUNCTION */
static int prog_cb (void *p, double dltotal, double dlnow, double ult,
                    double uln)
{
  (void)ult;
  (void)uln;

  return 0;
}

/* CURLOPT_OPENSOCKETFUNCTION */
static curl_socket_t opensocket(void *data,
                                curlsocktype purpose,
                                struct curl_sockaddr *address)
{
  HttpConnection *conn = static_cast<HttpConnection *>(data);
 
  curl_socket_t sockfd = CURL_SOCKET_BAD;

  /* restrict to ipv4 */
  if (purpose == CURLSOCKTYPE_IPCXN && address->family == AF_INET)
  {
      HttpClientSession *session = conn->CreateSession();
      if (session) {
          sockfd = session->socket()->native_handle();
          conn->set_session(session);
      }
  }

  return sockfd;
}

/* CURLOPT_CLOSESOCKETFUNCTION */
static int closesocket(void *clientp, curl_socket_t item)
{
  HttpConnection *conn = static_cast<HttpConnection *>(clientp);
  conn->delete_session();
  return 0;
}

static int send_perform(ConnInfo *conn, GlobalInfo *g) {
    // add the handle
    CURLMcode m_rc = curl_multi_add_handle(g->multi, conn->easy);
    if (m_rc != CURLM_OK)
        return m_rc;

    // start sending data rightaway; use timer to re-invoke multi_perform
    int counter = 0;
    CURLMcode rc = curl_multi_perform(g->multi, &counter);
    if (rc == CURLM_OK && counter <= 0) {
        // send done; invoke callback to indicate this
        if (conn->connection && conn->connection->session()) {
            const boost::system::error_code ec;
            event_cb(g, TcpSessionPtr(conn->connection->session()), 0, ec, 0);
        }
    } else {
        // start timer and check for send completion on timeout
        g->client->StartTimer(HttpClient::kDefaultTimeout);
    }

    return rc;
}

void del_conn(HttpConnection *connection, GlobalInfo *g) {

    if (connection->session()) {
        tbb::mutex::scoped_lock lock(connection->session()->mutex());
        connection->session()->SetConnection(NULL);
    }

    struct _ConnInfo *curl_handle = connection->curl_handle();
    if (curl_handle) {
        connection->set_curl_handle(NULL);
        del_curl_handle(curl_handle, g);
    }
}

void del_curl_handle(ConnInfo *curl_handle, GlobalInfo *g) {
    if (curl_handle) {
        curl_easy_setopt(curl_handle->easy, CURLOPT_PRIVATE, NULL);
        curl_multi_remove_handle(g->multi, curl_handle->easy);
        curl_slist_free_all(curl_handle->headers);
        free(curl_handle->post);
        free(curl_handle->url);
        curl_easy_cleanup(curl_handle->easy);
        free(curl_handle);
    }   
}

/* Create a new easy handle, and add it to the global curl_multi */
ConnInfo *new_conn(HttpConnection *connection, GlobalInfo *g,
                   bool header, bool short_timeout, bool reuse)
{
  ConnInfo *conn = (ConnInfo *)calloc(1, sizeof(ConnInfo));
  memset(conn, 0, sizeof(ConnInfo));
  conn->error[CURL_ERROR_SIZE]='\0';

  conn->easy = curl_easy_init();

  if ( !conn->easy ) {
    return NULL;
  }
  conn->global = g;
  curl_easy_setopt(conn->easy, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(conn->easy, CURLOPT_WRITEFUNCTION, write_cb);
  curl_easy_setopt(conn->easy, CURLOPT_WRITEDATA, connection);
  curl_easy_setopt(conn->easy, CURLOPT_HEADERFUNCTION, header_cb);
  curl_easy_setopt(conn->easy, CURLOPT_HEADERDATA, connection);
  curl_easy_setopt(conn->easy, CURLOPT_READFUNCTION, read_cb);
  curl_easy_setopt(conn->easy, CURLOPT_READDATA, connection);
  curl_easy_setopt(conn->easy, CURLOPT_ERRORBUFFER, conn->error);
  curl_easy_setopt(conn->easy, CURLOPT_PRIVATE, conn);
  curl_easy_setopt(conn->easy, CURLOPT_NOPROGRESS, 1L);
  curl_easy_setopt(conn->easy, CURLOPT_PROGRESSFUNCTION, prog_cb);
  curl_easy_setopt(conn->easy, CURLOPT_PROGRESSDATA, conn);
  curl_easy_setopt(conn->easy, CURLOPT_CONNECTTIMEOUT, 4L); // in secs
  if (short_timeout) {
      /* set the timeout limits to abort the connection */
      curl_easy_setopt(conn->easy, CURLOPT_LOW_SPEED_TIME, 3L);
      curl_easy_setopt(conn->easy, CURLOPT_LOW_SPEED_LIMIT, 10L);
  } else {
      /* set longer timeout limits */
      curl_easy_setopt(conn->easy, CURLOPT_LOW_SPEED_TIME, 30L);
      curl_easy_setopt(conn->easy, CURLOPT_LOW_SPEED_LIMIT, 10L);
  }
  curl_easy_setopt(conn->easy, CURLOPT_FORBID_REUSE, 1L);

  /* call this function to get a socket */
  curl_easy_setopt(conn->easy, CURLOPT_OPENSOCKETFUNCTION, opensocket);
  curl_easy_setopt(conn->easy, CURLOPT_OPENSOCKETDATA, connection);

  /* call this function to close a socket */
  curl_easy_setopt(conn->easy, CURLOPT_CLOSESOCKETFUNCTION, closesocket);
  curl_easy_setopt(conn->easy, CURLOPT_CLOSESOCKETDATA, connection);

  return conn;
}

void set_url(ConnInfo *conn, const char *url) {
  conn->url = strdup(url);
  curl_easy_setopt(conn->easy, CURLOPT_URL, conn->url);
}

void set_header_options(ConnInfo *conn, const char *options) { 
    conn->headers = curl_slist_append(conn->headers, options);
    curl_easy_setopt(conn->easy, CURLOPT_HTTPHEADER, conn->headers);
}

void set_post_string(ConnInfo *conn, const char *post, uint32_t len) {
    conn->post = (char *) malloc(len);
    memcpy(conn->post, post, len);
    conn->post_len = len;
    curl_easy_setopt(conn->easy, CURLOPT_POST, 1);
    curl_easy_setopt(conn->easy, CURLOPT_POSTFIELDS, conn->post);
    curl_easy_setopt(conn->easy, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)len);
}

void set_put_string(ConnInfo *conn, const char *put, uint32_t len) {
    conn->post = (char *) malloc(len);
    memcpy(conn->post, put, len);
    conn->post_len = len;
    curl_easy_setopt(conn->easy, CURLOPT_UPLOAD, 1);
    curl_easy_setopt(conn->easy, CURLOPT_PUT, 1);
    curl_easy_setopt(conn->easy, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)len);
}

int http_get(ConnInfo *conn, GlobalInfo *g) {
    CURLMcode rc = curl_multi_add_handle(g->multi, conn->easy);
    return (int)rc;
}

int http_head(ConnInfo *conn, GlobalInfo *g) {
    curl_easy_setopt(conn->easy, CURLOPT_CUSTOMREQUEST, "HEAD");
    CURLMcode rc = curl_multi_add_handle(g->multi, conn->easy);
    return (int)rc;
}

int http_put(ConnInfo *conn, GlobalInfo *g) {
    return send_perform(conn, g);
}

int http_post(ConnInfo *conn, GlobalInfo *g) {
    return send_perform(conn, g);
}

int http_delete(ConnInfo *conn, GlobalInfo *g) {
    curl_easy_setopt(conn->easy, CURLOPT_CUSTOMREQUEST, "DELETE");
    CURLMcode rc = curl_multi_add_handle(g->multi, conn->easy);
    return (int)rc;
}

int curl_init(HttpClient *client)
{
  struct _GlobalInfo *g = client->GlobalInfo();

  memset(g, 0, sizeof(GlobalInfo));
  g->multi = curl_multi_init();
  g->client = client;

  curl_multi_setopt(g->multi, CURLMOPT_SOCKETFUNCTION, sock_cb);
  curl_multi_setopt(g->multi, CURLMOPT_SOCKETDATA, g);
  curl_multi_setopt(g->multi, CURLMOPT_TIMERFUNCTION, multi_timer_cb);
  curl_multi_setopt(g->multi, CURLMOPT_TIMERDATA, client);

  return 0;
}
