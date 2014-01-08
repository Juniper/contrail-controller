#!/usr/bin/python

#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

#
# log
#
# Query log messages from analytics
#

import sys
import argparse
import json
import datetime
from opserver.opserver_util import OpServerUtils
from sandesh_common.vns.ttypes import Module
from sandesh_common.vns.constants import ModuleNames, NodeTypeNames
import opserver.sandesh.viz.constants as VizConstants
from pysandesh.gen_py.sandesh.ttypes import SandeshType, SandeshLevel

OBJECT_TABLE_LIST = [table for table in VizConstants._OBJECT_TABLES]


class LogQuerier(object):

    def __init__(self):
        self._args = None
    # end __init__

    # Public functions
    def parse_args(self):
        """
        Eg. python log.py --opserver-ip 127.0.0.1
                          --opserver-port 8081
                          --source 127.0.0.1
                          --node-type Control
                          --module bgp | cfgm | vnswad
                          --instance-id 0
                          --message-type UveVirtualMachineConfigTrace
                          --category xmpp
                          --level SYS_INFO | SYS_ERROR
                          --object vn | vm
                          --object-id name
                          --object-select-field ObjectLog | SystemLog
                          --reverse
                          --verbose
                          --raw
                          --trace BgpPeerTraceBuf
                          [--start-time now-10m --end-time now] | --last 10m
        """
        defaults = {
            'opserver_ip': '127.0.0.1',
            'opserver_port': '8081',
            'start_time': 'now-10m',
            'end_time': 'now',
        }

        parser = argparse.ArgumentParser(
            formatter_class=argparse.ArgumentDefaultsHelpFormatter)
        parser.set_defaults(**defaults)
        parser.add_argument("--opserver-ip", help="IP address of OpServer")
        parser.add_argument("--opserver-port", help="Port of OpServer")
        parser.add_argument(
            "--start-time", help="Logs start time (format now-10m, now-1h)")
        parser.add_argument("--end-time", help="Logs end time")
        parser.add_argument(
            "--last", help="Logs from last time period (format 10m, 1d)")

        parser.add_argument("--source", help="Logs from source address")
        parser.add_argument("--node-type", help="Logs from node type",
            choices=NodeTypeNames.values())
        parser.add_argument(
            "--module", help="Logs from module", choices=ModuleNames.values())
        parser.add_argument("--instance-id", help="Logs from module instance")
        parser.add_argument("--category", help="Logs of category")
        parser.add_argument("--level", help="Logs of level")
        parser.add_argument("--message-type", help="Logs of message type")
        parser.add_argument("--reverse", action="store_true",
                            help="Show logs in reverse chronological order")
        parser.add_argument(
            "--verbose", action="store_true", help="Show internal information")
        parser.add_argument(
            "--all", action="store_true", help="Show all logs")
        parser.add_argument(
            "--raw", action="store_true", help="Show raw XML messages")

        parser.add_argument(
            "--object", help="Logs of object type", choices=OBJECT_TABLE_LIST)
        parser.add_argument("--object-id", help="Logs of object name")
        parser.add_argument(
            "--object-select-field", help="Select field to filter the log",
            choices=[VizConstants.OBJECT_LOG, VizConstants.SYSTEM_LOG])
        parser.add_argument("--trace", help="Dump trace buffer")
        parser.add_argument("--limit", help="Limit the number of messages")

        self._args = parser.parse_args()

        if self._args.last is not None:
            self._args.last = '-' + self._args.last
            self._start_time = OpServerUtils.convert_to_utc_timestamp_usec(
                self._args.last)
            self._end_time = OpServerUtils.convert_to_utc_timestamp_usec('now')
        else:
            try:
                if (self._args.start_time.isdigit() and
                        self._args.end_time.isdigit()):
                    self._start_time = int(self._args.start_time)
                    self._end_time = int(self._args.end_time)
                else:
                    self._start_time =\
                        OpServerUtils.convert_to_utc_timestamp_usec(
                            self._args.start_time)
                    self._end_time =\
                        OpServerUtils.convert_to_utc_timestamp_usec(
                            self._args.end_time)
            except:
                print 'Incorrect start-time (%s) or end-time (%s) format' %\
                    (self._args.start_time, self._args.end_time)
                return -1
        return 0
    # end parse_args

    # Public functions
    def query(self):
        start_time, end_time = OpServerUtils.get_start_end_time(
            self._start_time,
            self._end_time)
        messages_url = OpServerUtils.opserver_query_url(
            self._args.opserver_ip,
            self._args.opserver_port)
        where_msg = []
        where_obj = []
        filter = []
        if self._args.source is not None:
            source_match = OpServerUtils.Match(name=VizConstants.SOURCE,
                                               value=self._args.source,
                                               op=OpServerUtils.MatchOp.EQUAL)
            where_msg.append(source_match.__dict__)

        if self._args.module is not None:
            module_match = OpServerUtils.Match(name=VizConstants.MODULE,
                                               value=self._args.module,
                                               op=OpServerUtils.MatchOp.EQUAL)
            where_msg.append(module_match.__dict__)

        if self._args.category is not None:
            category_match = OpServerUtils.Match(
                name=VizConstants.CATEGORY,
                value=self._args.category,
                op=OpServerUtils.MatchOp.EQUAL)
            where_msg.append(category_match.__dict__)

        if self._args.message_type is not None:
            message_type_match = OpServerUtils.Match(
                name=VizConstants.MESSAGE_TYPE,
                value=self._args.message_type,
                op=OpServerUtils.MatchOp.EQUAL)
            where_msg.append(message_type_match.__dict__)

        if self._args.level is not None:
            level_match = OpServerUtils.Match(
                name=VizConstants.LEVEL,
                value=SandeshLevel._NAMES_TO_VALUES[self._args.level],
                op=OpServerUtils.MatchOp.GEQ)
            filter.append(level_match.__dict__)

        if self._args.node_type is not None:
            node_type_match = OpServerUtils.Match(
                name=VizConstants.NODE_TYPE,
                value=self._args.node_type,
                op=OpServerUtils.MatchOp.EQUAL)
            filter.append(node_type_match.__dict__)

        if self._args.instance_id is not None:
            instance_id_match = OpServerUtils.Match(
                name=VizConstants.INSTANCE_ID,
                value=self._args.instance_id,
                op=OpServerUtils.MatchOp.EQUAL)
            filter.append(instance_id_match.__dict__)

        if (self._args.object is not None or
            self._args.object_id is not None or
                self._args.object_select_field is not None):
            # Object Table Query
            where_obj = list(where_msg)

            if self._args.object is not None:
                if self._args.object in OBJECT_TABLE_LIST:
                    table = self._args.object
                else:
                    print 'Unknown object table [%s]' % (self._args.object)
                    return None
            else:
                print 'Object required for query'
                return None

            if self._args.object_id is not None:
                id_match = OpServerUtils.Match(name=OpServerUtils.OBJECT_ID,
                                               value=self._args.object_id,
                                               op=OpServerUtils.MatchOp.EQUAL)
                where_obj.append(id_match.__dict__)
            else:
                print 'Object id required for table [%s]' % (self._args.object)
                return None

            if self._args.object_select_field is not None:
                if ((self._args.object_select_field !=
                     VizConstants.OBJECT_LOG) and
                    (self._args.object_select_field !=
                     VizConstants.SYSTEM_LOG)):
                    print 'Invalid object-select-field. '\
                        'Valid values are "%s" or "%s"' \
                        % (VizConstants.OBJECT_LOG,
                           VizConstants.SYSTEM_LOG)
                    return None
                obj_sel_field = [self._args.object_select_field]
                self._args.object_select_field = obj_sel_field
            else:
                self._args.object_select_field = obj_sel_field = [
                    VizConstants.OBJECT_LOG, VizConstants.SYSTEM_LOG]

            where = [where_obj]

            select_list = [
                VizConstants.TIMESTAMP,
                VizConstants.SOURCE,
                VizConstants.MODULE,
                VizConstants.MESSAGE_TYPE,
            ] + obj_sel_field
        elif self._args.trace is not None:
            table = VizConstants.COLLECTOR_GLOBAL_TABLE
            if self._args.source is None:
                print 'Source is required for trace buffer dump'
                return None
            if self._args.module is None:
                print 'Module is required for trace buffer dump'
                return None
            trace_buf_match = OpServerUtils.Match(
                name=VizConstants.CATEGORY,
                value=self._args.trace,
                op=OpServerUtils.MatchOp.EQUAL)
            where_msg.append(trace_buf_match.__dict__)
            where = [where_msg]
            select_list = [
                VizConstants.TIMESTAMP,
                VizConstants.MESSAGE_TYPE,
                VizConstants.SEQUENCE_NUM,
                VizConstants.DATA
            ]
            sandesh_type_filter = OpServerUtils.Match(
                name=VizConstants.SANDESH_TYPE,
                value=str(
                    SandeshType.TRACE),
                op=OpServerUtils.MatchOp.EQUAL)
            filter.append(sandesh_type_filter.__dict__)
        else:
            # Message Table Query
            table = VizConstants.COLLECTOR_GLOBAL_TABLE
            # Message Table contains both systemlog and objectlog.
            # Add a filter to return only systemlogs
            if not self._args.all:
                sandesh_type_filter = OpServerUtils.Match(
                    name=VizConstants.SANDESH_TYPE,
                    value=str(
                        SandeshType.SYSTEM),
                    op=OpServerUtils.MatchOp.EQUAL)
                filter.append(sandesh_type_filter.__dict__)

            if len(where_msg):
                where = [where_msg]
            else:
                where = None

            select_list = [
                VizConstants.TIMESTAMP,
                VizConstants.SOURCE,
                VizConstants.MODULE,
                VizConstants.CATEGORY,
                VizConstants.MESSAGE_TYPE,
                VizConstants.SEQUENCE_NUM,
                VizConstants.DATA,
                VizConstants.SANDESH_TYPE,
                VizConstants.LEVEL,
                VizConstants.NODE_TYPE,
                VizConstants.INSTANCE_ID,
            ]

        if len(filter) == 0:
            filter = None

        # Add sort by timestamp
        if self._args.reverse:
            sort_op = OpServerUtils.SortOp.DESCENDING
        else:
            sort_op = OpServerUtils.SortOp.ASCENDING
        sort_fields = [VizConstants.TIMESTAMP]

        if self._args.limit:
            limit = int(self._args.limit)
        else:
            limit = None

        messages_query = OpServerUtils.Query(table,
                                             start_time=start_time,
                                             end_time=end_time,
                                             select_fields=select_list,
                                             where=where,
                                             filter=filter,
                                             sort=sort_op,
                                             sort_fields=sort_fields,
                                             limit=limit)
        if self._args.verbose:
            print 'Performing query: {0}'.format(
                json.dumps(messages_query.__dict__))
        print ''
        resp = OpServerUtils.post_url_http(
            messages_url, json.dumps(messages_query.__dict__))
        result = {}
        if resp is not None:
            resp = json.loads(resp)
            qid = resp['href'].rsplit('/', 1)[1]
            result = OpServerUtils.get_query_result(
                self._args.opserver_ip, self._args.opserver_port, qid)
        return result
    # end query

    def display(self, result):
        if result == [] or result is None:
            return
        messages_dict_list = result

        for messages_dict in messages_dict_list:

            if VizConstants.TIMESTAMP in messages_dict:
                message_dt = datetime.datetime.fromtimestamp(
                    int(messages_dict[VizConstants.TIMESTAMP]) /
                    OpServerUtils.USECS_IN_SEC)
                message_dt += datetime.timedelta(
                    microseconds=
                    (int(messages_dict[VizConstants.TIMESTAMP]) %
                     OpServerUtils.USECS_IN_SEC))
                message_ts = message_dt.strftime(OpServerUtils.TIME_FORMAT_STR)
            else:
                message_ts = 'Time: NA'
            if VizConstants.SOURCE in messages_dict:
                source = messages_dict[VizConstants.SOURCE]
            else:
                source = 'Source: NA'
            if VizConstants.NODE_TYPE in messages_dict:
                node_type = messages_dict[VizConstants.NODE_TYPE]
            else:
                node_type = ''
            if VizConstants.MODULE in messages_dict:
                module = messages_dict[VizConstants.MODULE]
            else:
                module = 'Module: NA'
            if VizConstants.INSTANCE_ID in messages_dict:
                instance_id = messages_dict[VizConstants.INSTANCE_ID]
            else:
                instance_id = ''
            if VizConstants.MESSAGE_TYPE in messages_dict:
                message_type = messages_dict[VizConstants.MESSAGE_TYPE]
            else:
                message_type = 'Message Type: NA'
            if VizConstants.SANDESH_TYPE in messages_dict:
                sandesh_type = messages_dict[VizConstants.SANDESH_TYPE]
            else:
                sandesh_type = SandeshType.INVALID
            if self._args.object is None:
                if VizConstants.CATEGORY in messages_dict:
                    category = messages_dict[VizConstants.CATEGORY]
                else:
                    category = 'Category: NA'
                if VizConstants.LEVEL in messages_dict:
                    level = SandeshLevel._VALUES_TO_NAMES[
                        messages_dict[VizConstants.LEVEL]]
                else:
                    level = 'Level: NA'
                if VizConstants.SEQUENCE_NUM in messages_dict:
                    seq_num = messages_dict[VizConstants.SEQUENCE_NUM]
                else:
                    seq_num = 'Sequence Number: NA'
                if VizConstants.DATA in messages_dict:
                    # Convert XML data to dict
                    if self._args.raw:
                        data_str = messages_dict[VizConstants.DATA]
                    else:
                        OpServerUtils.messages_xml_data_to_dict(
                            messages_dict, VizConstants.DATA)
                        if isinstance(messages_dict[VizConstants.DATA], dict):
                            data_dict = messages_dict[VizConstants.DATA]
                            data_str = OpServerUtils.messages_data_dict_to_str(
                                data_dict, message_type, sandesh_type)
                        else:
                            data_str = messages_dict[VizConstants.DATA]
                else:
                    data_str = 'Data not present'
                if self._args.trace is not None:
                    print '{0} {1}:{2} {3}'.format(
                        message_ts, message_type, seq_num, data_str)
                else:
                    print '{0} {1} [{2}:{3}:{4}:{5}][{6}] : {7}:{8} {9}'.format(
                        message_ts, source, node_type, module, instance_id,
                        category, level, message_type, seq_num, data_str)
            else:
                for obj_sel_field in self._args.object_select_field:
                    if obj_sel_field in messages_dict:
                        if self._args.raw:
                            data_str = messages_dict[obj_sel_field]
                        else:
                            # Convert XML data to dict
                            OpServerUtils.messages_xml_data_to_dict(
                                messages_dict, obj_sel_field)
                            if isinstance(messages_dict[obj_sel_field], dict):
                                data_dict = messages_dict[obj_sel_field]
                                data_str =\
                                    OpServerUtils.messages_data_dict_to_str(
                                        data_dict, message_type,
                                        sandesh_type)
                            else:
                                data_str = messages_dict[obj_sel_field]
                        if data_str:
                            print '{0} {1} [{2}:{3}:{4}] : {5}: {6}'.format(
                                message_ts, source, node_type, module, instance_id,
                                message_type, data_str)
    # end display

# end class LogQuerier


def main():
    querier = LogQuerier()
    if querier.parse_args() != 0:
        return
    result = querier.query()
    querier.display(result)
# end main

if __name__ == "__main__":
    main()
