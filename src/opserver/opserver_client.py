#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

#
# Opserver Client
#
# Client of Operational State Server for VNC
#

import readline
import code
import urllib2
import time
import json
import datetime
import pdb
import argparse
import socket, struct
from prettytable import PrettyTable
from collections import OrderedDict
from opserver_util import OpServerUtils

class OpServerClient(object):
    """
        The OpServerClient object provides python functions to exercise :mod:`opserver` APIs

    """

    def __init__(self):
        self.opserver_time_range = 10*60*1000000 # 10 minutes in microseconds
    #end __init__

    def _parse_args(self):
        '''
        Eg. python opserver_client.py 10.1.5.139 8081
        '''
        parser = argparse.ArgumentParser()
        parser.add_argument("opserver_ip",
                            help = "IP address of OpServer")
        parser.add_argument("opserver_port",
                            help = "Port of OpServer (e.g. 8081)")
        self._args = parser.parse_args()
    #end _parse_args
    def UTCTimestampUsec(self):
        epoch = datetime.datetime.utcfromtimestamp(0)
        now = datetime.datetime.utcnow()
        delta = now-epoch
        return (int(delta.total_seconds()*1000000))
    #end UTCTimestampUsec

    def get_start_end_time(self, start_time, end_time):
        if start_time == None and end_time == None:
            end_time = self.UTCTimestampUsec()
            start_time = end_time - self.opserver_time_range
        elif start_time == None and end_time != None:
            start_time = end_time - self.opserver_time_range
        elif start_time != None and end_time == None:
            end_time = start_time + self.opserver_time_range
        return start_time, end_time
    #end get_start_end_time

    def get_url_http(self, url):
        data = {}
        try:
            data = urllib2.urlopen(url).read()
        except urllib2.HTTPError, e:
            print "HTTP error: %d" % e.code
        except urllib2.URLError, e:
            print "Network error: %s" % e.reason.args[1]

        return data
    #end get_url_http

    def opserver_url(self):
        return "http://" + self._args.opserver_ip + ":" + self._args.opserver_port + "/"
    #end opserver_url

    def opserver_analytics_url(self):
        return self.opserver_url() + 'analytics/'
    #end opserver_analytics_url

    def get_uve_state(self, name, uve_type) :
        """
        This function performs ReST API to :mod:`opserver` to get current
        state of a User Visible Entity

        :param name: name of the interested UVE
        :type name: str
        :param uve_type: type of the UVE - e.g virtual-network, vrouter etc.
        :type uve_type: str
        """
        uve_state_url = self.opserver_analytics_url() + uve_type + "/" + name
        print uve_state_url
        uve_state = json.loads(self.get_url_http(uve_state_url))
        print json.dumps(uve_state, indent = 4, sort_keys = True)


    def query(self, table, start_time = None, end_time = None,
            select_fields = None,
            where_clause = "",
            sort_fields = None, sort = None, limit = None, filter = None):
        """
        This function takes in the query parameters, format appropriately and calls
        ReST API to the :mod:`opserver` to get data

        :param table: table to do the query on
        :type table: str
        :param start_time: start_time of the query's timeperiod
        :type start_time: int
        :param end_time: end_time of the query's timeperiod
        :type end_time: int
        :param select_fields: list of columns to be returned in the final result
        :type select_fields: list of str
        :param where_clause: list of match conditions for the query
        :type where_clause: list of match, which is a pair of str ANDed
        :returns: str -- json formatted result
        :raises: Error

        """

        flows_url = OpServerUtils.opserver_query_url(self._args.opserver_ip,
                                                        self._args.opserver_port)
        print flows_url

        query_dict = OpServerUtils.get_query_dict(table, start_time, end_time,
            select_fields,
            where_clause,
            sort_fields, sort, limit, filter)

        print json.dumps(query_dict)
        resp = OpServerUtils.post_url_http(flows_url, json.dumps(query_dict))
        if resp is not None:
            resp = json.loads(resp)
            qid = resp['href'].rsplit('/', 1)[1]
            result = OpServerUtils.get_query_result(self._args.opserver_ip, 
                                                    self._args.opserver_port, qid)
            for item in result:
                print item

        return

    def xml_dict_remove_attributes(self, xml_dict):
        for k in list(xml_dict):
            if isinstance(xml_dict[k], dict):
                self.xml_dict_remove_attributes(xml_dict[k])
            if "@" in k or "line" in k or "file" in k:
                del xml_dict[k]
                continue
    #end xml_dict_remove_attributes

    def request_analyzer(self, analyzer_name):
        analyzer_req_url = self.opserver_url() + "request-analyzer/" + analyzer_name
        print analyzer_req_url
        print self.get_url_http(analyzer_req_url)
    #end request_analyzer

    def delete_analyzer(self, analyzer_name):
        analyzer_req_url = self.opserver_url() + "delete-analyzer/" + analyzer_name
        print analyzer_req_url
        print self.get_url_http(analyzer_req_url)
    #end delete_analyzer

    def show_analyzer(self, analyzer_name):
        analyzer_req_url = self.opserver_url() + "analyzers"
        if analyzer_name:
            analyzer_req_url += "?name=" + analyzer_name
        print analyzer_req_url
        print self.get_url_http(analyzer_req_url)
    #end show_analyzer

    def add_mirror_request(self, handle, apply_vn, analyzer_name,
                           src_vn=None, src_ip_prefix=None, src_ip_prefix_len=None,
                           dst_vn=None, dst_ip_prefix=None, dst_ip_prefix_len=None,
                           start_src_port=None, end_src_port=None,
                           start_dst_port=None, end_dst_port=None,
                           protocol=None, time_period=None):
        mirror_req_url = self.opserver_url() + "request-mirroring/" + handle + \
            "?apply_vn=" + apply_vn
        if src_vn:
            mirror_req_url += "&src_vn=%s" % (src_vn)
        if src_ip_prefix:
            mirror_req_url += "&src_ip_prefix=%s" % (src_ip_prefix)
        if src_ip_prefix_len:
            mirror_req_url += "&src_ip_prefix_len=%s" % (src_ip_prefix_len)
        if dst_vn:
            mirror_req_url += "&dst_vn=%s" % (dst_vn)
        if dst_ip_prefix:
            mirror_req_url += "&dst_ip_prefix=%s" % (dst_ip_prefix)
        if dst_ip_prefix_len:
            mirror_req_url += "&dst_ip_prefix_len=%s" % (dst_ip_prefix_len)
        if start_src_port:
            mirror_req_url += "&start_src_port=%s" % (start_src_port)
        if end_src_port:
            mirror_req_url += "&end_src_port=%s" % (end_src_port)
        if start_dst_port:
            mirror_req_url += "&start_dst_port=%s" % (start_dst_port)
        if end_dst_port:
            mirror_req_url += "&end_dst_port=%s" % (end_dst_port)
        if protocol:
            mirror_req_url += "&protocol=%s" % (protocol)
        if time_period:
            mirror_req_url += "&time_period=%s" % (time_period)
        mirror_req_url += "&analyzer_name=%s" % (analyzer_name)

        print mirror_req_url
        print self.get_url_http(mirror_req_url)
    #end add_mirror_request

    def delete_mirror_request(self, handle):
        mirror_req_url = self.opserver_url() + "delete-mirroring/" + handle
        print mirror_req_url
        print self.get_url_http(mirror_req_url)
    #end delete_mirror_request

    def show_mirror(self, handle):
        mirror_req_url = self.opserver_url() + "mirrors"
        if handle:
            mirror_req_url += "?name=" + handle
        print mirror_req_url
        print self.get_url_http(mirror_req_url)
    #end show_mirror

#end class OpServerClient

if __name__ == '__main__':
    client = OpServerClient()
    client._parse_args()
    vars = globals().copy()
    vars.update(locals())
    shell = code.InteractiveConsole(vars)
    shell.interact()
