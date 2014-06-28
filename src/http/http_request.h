/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __HTTP_REQUEST_H__
#define __HTTP_REQUEST_H__

#include <map>
#include <string>

#include "http_parser/http_parser.h"

class HttpRequest {
public:
    typedef std::map<std::string, std::string> HeaderMap;

    HttpRequest();

    void SetMethod(http_method method) { method_ = method; }
    http_method GetMethod() const { return method_; }
    // clobbers argument
    void SetUrl(std::string *url) {
	url_.swap(*url);
    }
    void PushHeader(const std::string &key, const std::string &value) {
	headers_.insert(make_pair(key, value));
    }
    void SetBody(const char *data, size_t length) {
        body_.append(data, length);
    }

    std::string ToString() const;

    std::string UrlPath() const;
    std::string UrlQuery() const;
    const HeaderMap & Headers() const { return headers_; }
    const std::string & Body() const { return body_; }
private:
    http_method method_;
    std::string url_;
    HeaderMap headers_;
    std::string body_;
};

#endif
