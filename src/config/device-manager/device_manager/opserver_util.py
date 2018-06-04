#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

#
# OpServer Utils
#
# Utility functions for Operational State Server for VNC
#

import requests
import pkg_resources
import json
from requests.auth import HTTPBasicAuth


def enum(**enums):
    return type('Enum', (), enums)
# end enum


class OpServerUtils(object):
    POST_HEADERS = {'Content-type': 'application/json; charset="UTF-8"',
                    'Expect': '202-accepted'}
    POST_HEADERS_SYNC = {'Content-type': 'application/json; charset="UTF-8"'}

    @staticmethod
    def post_url_http(logger, url, params, user, password, sync=False,
                      headers=None):
        if sync:
            hdrs = OpServerUtils.POST_HEADERS_SYNC
            stm = False
            pre = True
        else:
            hdrs = OpServerUtils.POST_HEADERS
            stm = True
            pre = False
        if headers:
            hdrs.update(headers)
        try:
            if int(pkg_resources.get_distribution("requests").version[0]) != 0:
                response = requests.post(url, stream=stm,
                                         data=params,
                                         auth=HTTPBasicAuth(user, password),
                                         headers=hdrs)
            else:
                response = requests.post(url, prefetch=pre,
                                         data=params,
                                         auth=HTTPBasicAuth(user, password),
                                         headers=hdrs)
        except requests.exceptions.ConnectionError, e:
            logger.error("Connection to %s failed %s", (url, str(e)))
            return None
        if response.status_code == 202 or response.status_code == 200:
            return response.text
        else:
            logger.error("HTTP error code: %d", response.status_code)
        return None
    # end post_url_http

    @staticmethod
    def opserver_url(ip, port):
        return "http://" + ip + ":" + port
    # end opserver_url

    @staticmethod
    def opserver_query_url(opserver_ip, opserver_port):
        return "http://" + opserver_ip + ":" + opserver_port + \
               "/analytics/query"
    # end opserver_query_url

    class Query(object):
        table = None
        start_time = None
        end_time = None
        select_fields = None
        where = None
        sort = None
        sort_fields = None
        limit = None
        filter = None
        dir = None
        is_service_instance = None
        session_type = None

        def __init__(self, table, start_time, end_time, select_fields,
                     where=None, sort_fields=None, sort=None, limit=None,
                     filter=None, dir=None, is_service_instance=None,
                     session_type=None):
            self.table = table
            self.start_time = start_time
            self.end_time = end_time
            self.select_fields = select_fields
            if dir is not None:
                self.dir = dir
            if where is not None:
                self.where = where
            if sort_fields is not None:
                self.sort_fields = sort_fields
            if sort is not None:
                self.sort = sort
            if limit is not None:
                self.limit = limit
            if filter is not None:
                self.filter = filter
            if is_service_instance is not None:
                self.is_service_instance = is_service_instance
            if session_type is not None:
                self.session_type = session_type
        # end __init__
    # end class Query

    MatchOp = enum(EQUAL=1, NOT_EQUAL=2, IN_RANGE=3,
                   NOT_IN_RANGE=4, LEQ=5, GEQ=6, PREFIX=7, REGEX_MATCH=8,
                   CONTAINS=9)

    SortOp = enum(ASCENDING=1, DESCENDING=2)

    class Match(object):
        name = None
        value = None
        op = None
        value2 = None

        def __init__(self, name, value, op, value2=None, suffix=None):
            self.name = name
            try:
                self.value = json.loads(value)
            except:
                self.value = value

            self.op = op
            try:
                self.value2 = json.loads(value2)
            except:
                self.value2 = value2

            if suffix:
                self.suffix = suffix.__dict__
            else:
                self.suffix = None
        # end __init__
    # end class Match
# end class OpServerUtils
