#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

#
# OpServer Utils
#
# Utility functions for Operational State Server for VNC
#

from gevent import monkey
monkey.patch_all()
import datetime
import time
import requests
import pkg_resources
import xmltodict
import json
import gevent
import socket, struct
import copy
import traceback
import ast
import re
try:
    from pysandesh.gen_py.sandesh.ttypes import SandeshType
except:
    class SandeshType(object):
        SYSTEM = 1
        TRACE = 4
from requests.auth import HTTPBasicAuth
try:
    from collections import OrderedDict
except ImportError:
    # python 2.6 or earlier, use backport
    from ordereddict import OrderedDict

def enum(**enums):
    return type('Enum', (), enums)
# end enum


def camel_case_to_hyphen(name):
    name = re.sub('(.)([A-Z][a-z]+)', r'\1-\2', name)
    name = re.sub('([a-z])([A-Z])', r'\1-\2', name).lower()
    return name
# end camel_case_to_hyphen


def inverse_dict(d):
    return dict(zip(d.values(), d.keys()))
# end inverse_dict


class ServicePoller(gevent.Greenlet):
    def __init__(self, logger, trace_cls, disc, svc_name, callbk, snh):
        gevent.Greenlet.__init__(self)
        self.disc = disc
        self.svc_name = svc_name
        self.logger = logger
        self.trace_cls = trace_cls
        self.callbk = callbk
        self.snh = snh

    def _run(self):
        old_list = []
        while True:
            svc_list = []
            try:
                sub_obj = \
                    self.disc.subscribe(self.svc_name, 0)
                slist= sub_obj.info 
            except Exception as ex:
                self.logger.error('Failed to get svc list %s from ' \
                                   'discovery server : %s %s' % \
                    (self.svc_name, str(ex.args), traceback.format_exc()))
            else:
                if isinstance(slist,list):
                    disc_trace = self.trace_cls()
                    disc_trace.publishers = []
                    for svc in slist:
                        selem = OrderedDict(sorted(svc.items()))
                        svc_list.append(selem)
                        disc_trace.publishers.append(str(selem))
                    disc_trace.trace_msg(name='DiscoveryMsg', sandesh = self.snh)
                    if old_list != sorted(svc_list):
                        self.callbk(svc_list)
                    old_list = copy.deepcopy(sorted(svc_list))
         
            gevent.sleep(10)

class OpServerUtils(object):

    TIME_FORMAT_STR = '%Y %b %d %H:%M:%S.%f'
    DEFAULT_TIME_DELTA = 10 * 60 * 1000000  # 10 minutes in microseconds
    USECS_IN_SEC = 1000 * 1000
    OBJECT_ID = 'ObjectId'
    POST_HEADERS = {'Content-type': 'application/json; charset="UTF-8"',
                    'Expect': '202-accepted'}
    POST_HEADERS_SYNC = {'Content-type': 'application/json; charset="UTF-8"'}
    TunnelType = enum(INVALID=0, MPLS_GRE=1, MPLS_UDP=2, VXLAN=3)

    @staticmethod
    def _get_list_name(lst):
        sname = ""
        for sattr in lst.keys():
            if sattr[0] not in ['@']:
                sname = sattr
        return sname

    @staticmethod
    def uve_attr_flatten(inp):
        sname = ""
        if (inp['@type'] == 'struct'):
            sname = OpServerUtils._get_list_name(inp)
            if (sname == ""):
                raise Exception('Struct Parse Error')
            ret = {}
            if inp[sname] is None:
                return ret
            for k, v in inp[sname].items():
                ret[k] = OpServerUtils.uve_attr_flatten(v)
            return ret
        elif (inp['@type'] == 'list'):
            sname = OpServerUtils._get_list_name(inp['list'])
            ret = []
            if (sname == ""):
                return ret
            items = inp['list'][sname]
            if not isinstance(items, list):
                items = [items]
            lst = []
            for elem in items:
                if not isinstance(elem, dict):
                    lst.append(elem)
                else:
                    lst_elem = {}
                    for k, v in elem.items():
                        lst_elem[k] = OpServerUtils.uve_attr_flatten(v)
                    lst.append(lst_elem)
            #ret[sname] = lst
            ret = lst
            return ret
        elif (inp['@type'] == 'map'):
            fmap = {}

            sname = None
            for ss in inp['map'].keys():
                if ss[0] != '@':
                    if ss != 'element':
                        sname = ss
            if sname is None:
                for idx in range(0,int(inp['map']['@size'])):
                    m_attr = inp['map']['element'][idx*2]
                    m_val = str(inp['map']['element'][(idx*2)+1])
                    fmap[m_attr] = m_val
            else:
                if not isinstance(inp['map']['element'], list):
                    inp['map']['element'] = [inp['map']['element']]
                if not isinstance(inp['map'][sname], list):
                    inp['map'][sname] = [inp['map'][sname]]
                for idx in range(0,int(inp['map']['@size'])):
                    m_attr = inp['map']['element'][idx]
                    subst = {}
                    if inp['map'][sname][idx]:
                        for sk,sv in inp['map'][sname][idx].iteritems():
                            subst[sk] = OpServerUtils.uve_attr_flatten(sv)
                    fmap[m_attr] = subst
            return fmap
        else:
            if '#text' not in inp:
                return None
            if inp['@type'] in ['i16', 'i32', 'i64', 'byte',
                                'u64', 'u32', 'u16']:
                return int(inp['#text'])
            elif inp['@type'] in ['float', 'double']:
                return float(inp['#text'])
            elif inp['@type'] in ['bool']:
                if inp['#text'] in ["false"]:
                    return False
                elif inp['#text'] in ["true"]:
                    return True
                else:
                    return inp['#text']
            else:
                return inp['#text']

    @staticmethod
    def utc_timestamp_usec():
        epoch = datetime.datetime.utcfromtimestamp(0)
        now = datetime.datetime.utcnow()
        delta = now - epoch
        return (delta.microseconds +
                (delta.seconds + delta.days * 24 * 3600) * 10 ** 6)
    # end utc_timestamp_usec

    @staticmethod
    def parse_start_end_time(start_time, end_time, last):
        ostart_time = None
        oend_time = None
        if last is not None:
            last = '-' + last
            ostart_time = 'now' + last
            oend_time = 'now'
        else:
            try:
                if 'now' in start_time and \
                        'now' in end_time:
                    ostart_time = start_time
                    oend_time = end_time
                elif start_time.isdigit() and \
                        end_time.isdigit():
                    ostart_time = int(start_time)
                    oend_time = int(end_time)
                else:
                    ostart_time =\
                        OpServerUtils.convert_to_utc_timestamp_usec(
                            start_time)
                    oend_time =\
                        OpServerUtils.convert_to_utc_timestamp_usec(
                            end_time)
            except:
                print 'Incorrect start-time (%s) or end-time (%s) format' %\
                    (start_time, end_time)
                raise

        if ostart_time is None and oend_time is None:
            oend_time = OpServerUtils.utc_timestamp_usec()
            ostart_time = end_time - OpServerUtils.DEFAULT_TIME_DELTA
        elif ostart_time is None and oend_time is not None:
            ostart_time = oend_time - OpServerUtils.DEFAULT_TIME_DELTA
        elif ostart_time is not None and oend_time is None:
            oend_time = ostart_time + OpServerUtils.DEFAULT_TIME_DELTA

        if 'now' in str(ostart_time) and 'now' in str(oend_time):
            now = OpServerUtils.utc_timestamp_usec()
            td = OpServerUtils.convert_to_time_delta(ostart_time[len('now'):])
            if td == None:
                ostart_time = now
            else:
                ostart_time = now + (td.microseconds + (td.seconds + td.days * 24 * 3600) * 10 ** 6)
            td = OpServerUtils.convert_to_time_delta(oend_time[len('now'):])
            if td == None:
                oend_time = now
            else:
                oend_time = now + (td.microseconds + (td.seconds + td.days * 24 * 3600) * 10 ** 6)

        return ostart_time, oend_time
    # end parse_start_end_time

    @staticmethod
    def post_url_http(url, params, user, password, sync=False):
        if sync:
            hdrs = OpServerUtils.POST_HEADERS_SYNC
            stm = False
            pre = True
        else:
            hdrs = OpServerUtils.POST_HEADERS
            stm = True
            pre = False

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
            print "Connection to %s failed %s" % (url, str(e))
            return None
        if (response.status_code == 202) or (response.status_code) == 200:
            return response.text
        else:
            print "HTTP error code: %d" % response.status_code
        return None
    # end post_url_http

    @staticmethod
    def get_url_http(url, user, password):
        data = {}
        try:
            if int(pkg_resources.get_distribution("requests").version[0]) != 0:
                data = requests.get(url, stream=True,
                                    auth=HTTPBasicAuth(user, password))
            else:
                data = requests.get(url, prefetch=False,
                                    auth=HTTPBasicAuth(user, password))
        except requests.exceptions.ConnectionError, e:
            print "Connection to %s failed %s" % (url, str(e))

        return data
    # end get_url_http

    @staticmethod
    def parse_query_result(result):
        done = False
        resit = result.iter_lines()
        while not done:
            try:
                ln = resit.next()
                if ln == '{"value": [':
                    continue
                if ln == ']}':
                    done = True
                    continue
                if ln[0] == ',':
                    out_line = '[ {} ' + ln + ' ]'
                else:
                    out_line = '[ {} , ' + ln + ' ]'

                out_list = json.loads(out_line)
                out_list.pop(0)
                for i in out_list:
                    yield i
            except Exception as e:
                print "Error parsing %s results: %s" % (ln, str(e))
                done = True
        return
    # end parse_query_result

    @staticmethod
    def get_query_result(opserver_ip, opserver_port, qid, user, password,
                         time_out=None):
        sleep_interval = 0.5
        time_left = time_out
        while True:
            url = OpServerUtils.opserver_query_url(
                opserver_ip, opserver_port) + '/' + qid
            resp = OpServerUtils.get_url_http(url, user, password)
            if resp.status_code != 200:
                yield {}
                return
            status = json.loads(resp.text)
            if status['progress'] < 0:
                print 'Error in query processing'
                return
            elif status['progress'] != 100:
                if time_out is not None:
                    if time_left > 0:
                        time_left -= sleep_interval
                    else:
                        print 'query timed out'
                        yield {}
                        return

                gevent.sleep(sleep_interval)
                continue
            else:
                for chunk in status['chunks']:
                    url = OpServerUtils.opserver_url(
                        opserver_ip, opserver_port) + chunk['href']
                    resp = OpServerUtils.get_url_http(url, user, password)
                    if resp.status_code != 200:
                        yield {}
                    else:
                        for result in OpServerUtils.parse_query_result(resp):
                            yield result
                return
    # end get_query_result

    @staticmethod
    def convert_to_time_delta(time_str):
        if time_str == '' or time_str == None:
            return None
        num = int(time_str[:-1])
        if time_str.endswith('s'):
            return datetime.timedelta(seconds=num)
        elif time_str.endswith('m'):
            return datetime.timedelta(minutes=num)
        elif time_str.endswith('h'):
            return datetime.timedelta(hours=num)
        elif time_str.endswith('d'):
            return datetime.timedelta(days=num)
    # end convert_to_time_delta

    @staticmethod
    def convert_to_utc_timestamp_usec(time_str):
        # First try datetime.datetime.strptime format
        try:
            dt = datetime.datetime.strptime(
                time_str, OpServerUtils.TIME_FORMAT_STR)
        except ValueError:
            # Try now-+ format
            if time_str == 'now':
                return OpServerUtils.utc_timestamp_usec()
            else:
                # Handle now-/+1h format
                if time_str.startswith('now'):
                    td = OpServerUtils.convert_to_time_delta(
                        time_str[len('now'):])
                else:
                    # Handle -/+1h format
                    td = OpServerUtils.convert_to_time_delta(time_str)

                utc_tstamp_usec = OpServerUtils.utc_timestamp_usec()
                return utc_tstamp_usec +\
                    ((td.microseconds +
                     (td.seconds + td.days * 24 * 3600) * 10 ** 6))
        else:
            return int(time.mktime(dt.timetuple()) * 10 ** 6)
    # end convert_to_utc_timestamp_usec

    @staticmethod
    def tunnel_type_to_str(tunnel_type):
        if tunnel_type == OpServerUtils.TunnelType.MPLS_GRE:
            return "MPLSoGRE"
        elif tunnel_type == OpServerUtils.TunnelType.MPLS_UDP:
            return "MPLSoUDP"
        elif tunnel_type == OpServerUtils.TunnelType.VXLAN:
            return "VXLAN"
        else:
            return str(tunnel_type)
    #end tunnel_type_to_str

    @staticmethod
    def tunnel_type_to_protocol(tunnel_type):
        if tunnel_type == OpServerUtils.TunnelType.MPLS_GRE:
            return 47
        elif tunnel_type == OpServerUtils.TunnelType.MPLS_UDP:
            return 17
        elif tunnel_type == OpServerUtils.TunnelType.VXLAN:
            return 17
        return tunnel_type
    # end tunnel_type_to_protocol

    @staticmethod
    def ip_protocol_to_str(protocol):
        if protocol == 6:
            return "TCP"
        elif protocol == 17:
            return "UDP"
        elif protocol == 1:
            return "ICMP"
        elif protocol == 2:
            return "IGMP"
        else:
            return str(protocol)
    #end ip_protocol_to_str

    @staticmethod
    def str_to_ip_protocol(protocol):
        if protocol.lower() == "tcp":
            return 6
        elif protocol.lower() == "udp":
            return 17
        elif protocol.lower() == "icmp":
            return 1
        elif protocol.lower() == "igmp":
            return 2
        else:
            return -1
    #end str_to_ip_protocol

    @staticmethod
    def opserver_url(ip, port):
        return "http://" + ip + ":" + port
    # end opserver_url

    @staticmethod
    def opserver_query_url(opserver_ip, opserver_port):
        return "http://" + opserver_ip + ":" + opserver_port +\
            "/analytics/query"
    # end opserver_query_url

    @staticmethod
    def opserver_database_purge_query_url(opserver_ip, opserver_port):
        return "http://" + opserver_ip + ":" + opserver_port +\
            "/analytics/operation/database-purge"
    # end opserver_database_purge_query_url

    @staticmethod
    def messages_xml_data_to_dict(messages_dict, msg_type):
        if msg_type in messages_dict:
            # convert xml value to dict
            try:
                messages_dict[msg_type] = xmltodict.parse(
                    messages_dict[msg_type])
            except:
                pass
    # end messages_xml_data_to_dict

    @staticmethod
    def _messages_dict_remove_keys(messages_dict, key_pattern):
        for key, value in messages_dict.items():
            if key_pattern in key:
                del messages_dict[key]
            if isinstance(value, list):
                for elem in value:
                    if isinstance(elem, dict):
                        OpServerUtils._messages_dict_remove_keys(
                            elem, key_pattern)
            if isinstance(value, dict):
                OpServerUtils._messages_dict_remove_keys(value, key_pattern)
    # end _messages_dict_remove_keys

    @staticmethod
    def _messages_dict_flatten_key(messages_dict, key_match):
        for key, value in messages_dict.items():
            if isinstance(value, dict):
                if key_match in value:
                    messages_dict[key] = value[key_match]
                else:
                    OpServerUtils._messages_dict_flatten_key(value, key_match)
            if isinstance(value, list):
                for elem in value:
                    if isinstance(elem, dict):
                        OpServerUtils._messages_dict_flatten_key(elem,
                            key_match)
    #end _messages_dict_flatten_key

    @staticmethod
    def _json_loads_check(value):
        try:
            json_value = json.loads(value)
        except:
            return False, None
        else:
            return True, json_value
    # end _json_loads_check

    @staticmethod
    def _eval_check(value):
        try:
            eval_value = ast.literal_eval(value)
        except:
            return False, None
        else:
            return True, eval_value
    # end _eval_check

    # Evaluate messages dict using json.loads() or ast.literal_eval()
    @staticmethod
    def _messages_dict_eval(messages_dict):
        for key, value in messages_dict.iteritems():
            if isinstance(value, basestring):
                # First try json.loads
                success, json_value = OpServerUtils._json_loads_check(value)
                if success:
                    messages_dict[key] = json_value
                    continue
                # Next try ast.literal_eval
                success, eval_value = OpServerUtils._eval_check(value)
                if success:
                    messages_dict[key] = eval_value
                    continue
            if isinstance(value, dict):
                OpServerUtils._messages_dict_eval(value)
            if isinstance(value, list):
                for elem in value:
                    if isinstance(elem, dict):
                        OpServerUtils._messages_dict_eval(elem)
    # end _messages_dict_eval

    # Scrubs the message dict to remove keys containing @, and will
    # flatten out dicts containing #text and eval strings
    @staticmethod
    def messages_dict_scrub(messages_dict):
        OpServerUtils._messages_dict_remove_keys(messages_dict, '@')
        OpServerUtils._messages_dict_flatten_key(messages_dict, '#text')
        OpServerUtils._messages_dict_eval(messages_dict)
    # end messages_dict_scrub

    @staticmethod
    def messages_data_dict_to_str(messages_dict, message_type, sandesh_type):
        data_dict = messages_dict[message_type]
        if sandesh_type == SandeshType.SYSLOG:
            return data_dict.encode('utf8', 'replace')
        return OpServerUtils._data_dict_to_str(data_dict, sandesh_type)
    # end messages_data_dict_to_str

    @staticmethod
    def _data_dict_to_str(data_dict, sandesh_type):
        data_str = None
        if not data_dict:
            return ''
        for key, value in data_dict.iteritems():
            # Ignore if type is sandesh
            if '@type' == key and value == 'sandesh':
                continue
            # Do not print 'file' and 'line'
            if 'file' == key or 'line' == key:
                continue
            # Confirm value is dict
            if isinstance(value, dict):
                value_dict = value
            else:
                continue

            # Handle struct, list, ipv4 type
            if '@type' in value_dict:
                if value_dict['@type'] == 'ipv4':
                    elem = int(value_dict['#text'])
                    value_dict['#text'] = socket.inet_ntoa(struct.pack('!L',elem))
                if value_dict['@type'] == 'struct':
                    for vdict_key, vdict_value in value_dict.iteritems():
                        if isinstance(vdict_value, dict):
                            if data_str is None:
                                data_str = ''
                            else:
                                data_str += ', '
                            data_str += \
                                '[' + vdict_key + ': ' +\
                                OpServerUtils._data_dict_to_str(
                                    vdict_value, sandesh_type) + ']'
                    continue
                if value_dict['@type'] == 'list':
                    if data_str is None:
                        data_str = ''
                    else:
                        data_str += ', '
                    vlist_dict = value_dict['list']
                    # Handle list of basic types
                    if 'element' in vlist_dict:
                        if not isinstance(vlist_dict['element'], list):
                            velem_list = [vlist_dict['element']]
                        else:
                            velem_list = vlist_dict['element']
                        data_str += '[' + key + ':'
                        if vlist_dict['@type'] == 'ipv4':
                            for velem in velem_list:
                                velem = socket.inet_ntoa(struct.pack('!L',int(velem)))
                                data_str += ' ' + velem
                        else:
                            for velem in velem_list:
                                data_str += ' ' + str(velem)
                        data_str += ']'
                    # Handle list of complex types
                    else:
                        data_str += '[' + key + ':'
                        for vlist_key, vlist_value in vlist_dict.iteritems():
                            if isinstance(vlist_value, dict):
                                vlist_value_list = [vlist_value]
                            elif isinstance(vlist_value, list):
                                vlist_value_list = vlist_value
                            else:
                                continue
                            for vdict in vlist_value_list:
                                data_str +=\
                                    ' [' + OpServerUtils._data_dict_to_str(
                                        vdict, sandesh_type) + ']'
                        data_str += ']'
                    continue
                if value_dict['@type'] == 'map':
                    if data_str is None:
                        data_str = ''
                    else:
                         data_str += ', '
                    vdict = value_dict['map']
                    data_str += key + ': {'

                    sname = None
                    for ss in vdict.keys():
                        if ss[0] != '@':
                            if ss != 'element':
                                sname = ss

                    if sname is not None:
                        keys = []
                        values = []
                        for key, value in vdict.iteritems():
                            if key == 'element':
                                if isinstance(value, list):
                                    keys = value
                                else:
                                    keys = [value]
                            elif isinstance(value, dict):
                                values = [value]
                            elif isinstance(value, list):
                                values = value
                        for i in range(len(keys)):
                            data_str += keys[i] + ': ' + \
                                '[' + OpServerUtils._data_dict_to_str(
                                    values[i], sandesh_type) + '], '
                    else:
                        if 'element' in vdict:
                            vdict_list = vdict['element']
                            for i in range(int(vdict['@size'])):
                                k = i*2
                                data_str += vdict_list[k] + ': ' + \
                                    vdict_list[k+1] + ', '
                    data_str += '}'
                    continue
            else:
                if data_str is None:
                    data_str = ''
                else:
                    data_str += ', '
                data_str += '[' + OpServerUtils._data_dict_to_str(
                    value_dict, sandesh_type) + ']'
                continue

            if (sandesh_type == SandeshType.SYSTEM or
                    sandesh_type == SandeshType.TRACE):
                if data_str is None:
                    data_str = ''
                else:
                    data_str += ' '
                if '#text' in value_dict:
                    data_str += value_dict['#text']
                if 'element' in value_dict:
                    data_str += value_dict['element']
            else:
                if data_str is None:
                    data_str = ''
                else:
                    data_str += ', '
                if '#text' in value_dict:
                    data_str += key + ' = ' + value_dict['#text']
                elif 'element' in value_dict:
                    data_str += key + ' = ' + value_dict['element']
                else:
                    data_str += key + ' = '

        if data_str is None:
            data_str = ''
        return data_str
    # end _data_dict_to_str

    @staticmethod
    def get_query_dict(table, start_time=None, end_time=None,
                       select_fields=None,
                       where_clause="",
                       sort_fields=None, sort=None, limit=None, filter=None, dir=None):
        """
        This function takes in the query parameters,
        format appropriately and calls
        ReST API to the :mod:`opserver` to get data

        :param table: table to do the query on
        :type table: str
        :param start_time: start_time of the query's timeperiod
        :type start_time: int
        :param end_time: end_time of the query's timeperiod
        :type end_time: int
        :param select_fields: list of columns to be returned
            in the final result
        :type select_fields: list of str
        :param where_clause: list of match conditions for the query
        :type where_clause: list of match, which is a pair of str ANDed
        :returns: str -- dict of query request
        :raises: Error

        """
        try:
            lstart_time, lend_time = OpServerUtils.parse_start_end_time(
                                         start_time = start_time,
                                         end_time = end_time,
                                         last = None)
        except:
            return None

        sf = select_fields
        where = []
        for term in where_clause.split('OR'):
            term_elem = []
            for match in term.split('AND'):
                if match == '':
                    continue
                match_s = match.strip(' ()')
                match_e = match_s.split('=')
                match_e[0] = match_e[0].strip(' ()')
                match_e[1] = match_e[1].strip(' ()')

                match_sp = match_e[0].split('|')

                if len(match_sp) is 1:
                    tname = match_sp[0]
                    match_v = match_e[1].split("<")
                else:
                    tname = match_sp[1]
                    bname = match_sp[0]
                    match_vp = match_e[1].split('|')
                    bval = match_vp[0]
                    match_v = match_vp[1].split("<")

                
                if len(match_v) is 1:
                    if match_v[0][-1] is '*':
                        match_prefix = match_v[0][:(len(match_v[0]) - 1)]
                        print match_prefix
                        match_elem = OpServerUtils.Match(
                            name=tname, value=match_prefix,
                            op=OpServerUtils.MatchOp.PREFIX)
                    else:
                        match_elem = OpServerUtils.Match(
                            name=tname, value=match_v[0],
                            op=OpServerUtils.MatchOp.EQUAL)
                else:
                    match_elem = OpServerUtils.Match(
                        name=tname, value=match_v[0],
                        op=OpServerUtils.MatchOp.IN_RANGE, value2=match_v[1])

                if len(match_sp) is 1:
                    term_elem.append(match_elem.__dict__)
                else:
                    selem = OpServerUtils.Match(
                        name=bname, value=bval, op=OpServerUtils.MatchOp.EQUAL,
                        value2=None, suffix = match_elem)
                    term_elem.append(selem.__dict__)

            if len(term_elem) == 0:
                where = None
            else:
                where.append(term_elem)

        filter_terms = []
        if filter is not None:
            for match in filter.split(','):
                match_s = match.strip(' ()')
                match_e = match_s.split('=')
                match_op = ["", ""]
                if (len(match_e) == 2):
                    match_op[0] = match_e[0].strip(' ()')
                    match_op[1] = match_e[1].strip(' ()')
                    op = OpServerUtils.MatchOp.REGEX_MATCH

                match_e = match_s.split('<=')
                if (len(match_e) == 2):
                    match_op[0] = match_e[0].strip(' ()')
                    match_op[1] = match_e[1].strip(' ()')
                    op = OpServerUtils.MatchOp.LEQ

                match_e = match_s.split('>=')
                if (len(match_e) == 2):
                    match_op[0] = match_e[0].strip(' ()')
                    match_op[1] = match_e[1].strip(' ()')
                    op = OpServerUtils.MatchOp.GEQ

                match_elem = OpServerUtils.Match(name=match_op[0],
                                                 value=match_op[1],
                                                 op=op)
                filter_terms.append(match_elem.__dict__)

        if len(filter_terms) == 0:
            filter_terms = None
        if table == "FlowSeriesTable" or table == "FlowRecordTable":
            if dir is None:
                 dir = 1
        qe_query = OpServerUtils.Query(table,
                        start_time=lstart_time,
                        end_time=lend_time,
                        select_fields=sf,
                        where=where,
                        sort_fields=sort_fields,
                        sort=sort,
                        limit=limit,
                        filter=filter_terms,
                        dir=dir)

        return qe_query.__dict__

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

        def __init__(self, table, start_time, end_time, select_fields,
                     where=None, sort_fields=None, sort=None, limit=None,
                     filter=None, dir=None):
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
        # end __init__

    # end class Query

    MatchOp = enum(EQUAL=1, NOT_EQUAL=2, IN_RANGE=3,
                   NOT_IN_RANGE=4, LEQ=5, GEQ=6, PREFIX=7, REGEX_MATCH=8)

    SortOp = enum(ASCENDING=1, DESCENDING=2)

    class Match(object):
        name = None
        value = None
        op = None
        value2 = None

        def __init__(self, name, value, op, value2=None, suffix = None):
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
