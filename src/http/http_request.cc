/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "http/http_request.h"

#include "base/util.h"

using namespace std;

HttpRequest::HttpRequest() :
    method_(static_cast<http_method>(-1)), event_(TcpSession::EVENT_NONE) {
}

string HttpRequest::ToString() const {
    if (!url_.empty()) {
        string repr = http_method_str(method_);
        repr += " ";
        repr += url_;
        return repr;
    }
    else return "";
}

string HttpRequest::UrlPath() const {
    struct http_parser_url urldata;
    string path;
    int res = http_parser_parse_url(url_.c_str(), url_.size(), false, &urldata);
    if (res == 0 && BitIsSet(urldata.field_set, UF_PATH)) {
	path = url_.substr(urldata.field_data[UF_PATH].off,
			   urldata.field_data[UF_PATH].len);
    }
    return path;
}

string HttpRequest::UrlQuery() const {
    struct http_parser_url urldata;
    string query;
    int res = http_parser_parse_url(url_.c_str(), url_.size(), false, &urldata);
    if (res == 0 && BitIsSet(urldata.field_set, UF_QUERY)) {
	query = url_.substr(urldata.field_data[UF_QUERY].off,
			   urldata.field_data[UF_QUERY].len);
    }
    return query;
}
