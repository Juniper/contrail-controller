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
import ConfigParser
import argparse
import json
import datetime
import logging
import logging.handlers
import time
import re
from multiprocessing import Process
from opserver_util import OpServerUtils
from sandesh_common.vns.ttypes import Module
from sandesh_common.vns.constants import ModuleNames, NodeTypeNames
import sandesh.viz.constants as VizConstants
from pysandesh.gen_py.sandesh.ttypes import SandeshType, SandeshLevel
from pysandesh.sandesh_logger import SandeshLogger
from pysandesh.util import UTCTimestampUsec
import commands
import ast

OBJECT_TYPE_LIST = [table_info.log_query_name for table_info in \
    VizConstants._OBJECT_TABLES.values()]
OBJECT_TABLE_MAP = dict((table_info.log_query_name, table_name) for \
   (table_name, table_info) in VizConstants._OBJECT_TABLES.items())
output_file_handle = None

class LogQuerier(object):

    def __init__(self):
        self._args = None
        self._slogger = None
    # end __init__

    def run(self):
        try:
            if self.parse_args() != 0:
                return

            if self._args.tail:
                start_time = UTCTimestampUsec() - 10*pow(10,6)
                while True:
                    self._start_time = start_time
                    self._end_time = UTCTimestampUsec()
                    start_time = self._end_time + 1
                    time.sleep(3)
                    result = self.query()
                    if result == -1:
                        return
                    self.display(result)
            else:
                start_time = self._args.start_time
                end_time = self._args.end_time
                if not self._args.start_time:
                    start_time = "now-10m"
                if not self._args.end_time:
                    end_time = "now"
                try:
                    self._start_time, self._end_time = \
                        OpServerUtils.parse_start_end_time(
                            start_time = start_time,
                            end_time = end_time,
                            last = self._args.last)
                except:
                    return -1

                start_time = self._start_time
                end_time = self._end_time

                result_list = []
                while int(end_time) - int(start_time) > 0:
                    if not self._args.reverse:
                        self._start_time = start_time
                        self._end_time =  start_time + 10*60*pow(10,6) if (start_time + 10*60*pow(10,6) <= int(end_time)) else int(end_time)
                    else:
                        self._end_time = end_time
                        self._start_time =  end_time - 10*60*pow(10,6) if (end_time - 10*60*pow(10,6) >= int(start_time)) else int(start_time)

                    p = Process(target=self.display, args=(result_list,))
                    p.start()
                    result = self.query()
                    if result == -1:
                        return
                    # Accumulate the result before processing it as the
                    # formatting of result can be cpu intensive and hence would
                    # affect the overall time taken to fetch the result from the
                    # analytics-api. Since the query result ttl is set to 5 min
                    # in redis, it is necessary to improve the read throughput.
                    result_list = self.read_result(result)
                    p.join()
                    if not self._args.reverse:
                        start_time = self._end_time + 1
                    else:
                        end_time = self._start_time - 1
                self.display(result_list)

        except KeyboardInterrupt:
            return

    # Public functions
    def parse_args(self):
        """
        Eg. python log.py --analytics-api-ip 127.0.0.1
                          --analytics-api-port 8181
                          --source 127.0.0.1
                          --node-type Control
                          --module bgp | cfgm | vnswad
                          --instance-id 0
                          --message-type UveVirtualMachineConfigTrace
                          --category xmpp
                          --level SYS_INFO | SYS_ERROR
                          --object-type virtual-network | virtual-machine
                          --object-id name
                          --object-select-field ObjectLog | SystemLog
                          --reverse
                          --verbose
                          --raw
                          --trace BgpPeerTraceBuf
                          [--start-time now-10m --end-time now] | --last 10m
                          --send-syslog
                          --syslog-server 127.0.0.1
                          --syslog-port 514
                          --keywords comma,seperated,list
        """
        defaults = {
            'analytics_api_ip': '127.0.0.1',
            'analytics_api_port': '8181',
            'admin_user': 'admin',
            'admin_password': 'contrail123',
            'conf_file': '/etc/contrail/contrail-keystone-auth.conf',
        }

        conf_parser = argparse.ArgumentParser(add_help=False)
        conf_parser.add_argument("--admin-user", help="Name of admin user")
        conf_parser.add_argument("--admin-password", help="Password of admin user")
        conf_parser.add_argument("--conf-file", help="Configuration file")
        conf_parser.add_argument("--analytics-api-ip", help="IP address of Analytics API Server")
        conf_parser.add_argument("--analytics-api-port", help="Port of Analytics API Server")
        args, remaining_argv = conf_parser.parse_known_args();

        configfile = defaults['conf_file']
        if args.conf_file:
            configfile = args.conf_file

        config = ConfigParser.SafeConfigParser()
        config.read(configfile)
        if 'KEYSTONE' in config.sections():
            if args.admin_user == None:
                args.admin_user = config.get('KEYSTONE', 'admin_user')
            if args.admin_password == None:
                args.admin_password = config.get('KEYSTONE','admin_password')

        if args.admin_user == None:
            args.admin_user = defaults['admin_user']
        if args.admin_password == None:
            args.admin_password = defaults['admin_password']

        if args.analytics_api_ip == None:
            args.analytics_api_ip = defaults['analytics_api_ip']
        if args.analytics_api_port == None:
            args.analytics_api_port = defaults['analytics_api_port']

        tab_url = "http://" + args.analytics_api_ip + ":" +\
            args.analytics_api_port + "/analytics/tables"
        tables = OpServerUtils.get_url_http(tab_url,
            args.admin_user, args.admin_password)
        if tables != {}:
            if tables.status_code == 200:
                table_list = json.loads(tables.text)
                for table in table_list:
                    if table['type'] == 'OBJECT':
                        # append to OBJECT_TYPE_LIST only if not existing
                        if table['name'] not in OBJECT_TABLE_MAP.values():
                            OBJECT_TYPE_LIST.append(str(table['name']))
                            # For object table the mapping between the actual table
                            # name and the table name used in help msg are the same
                            OBJECT_TABLE_MAP[table['name']]=table['name']

        parser = argparse.ArgumentParser(
                  # Inherit options from config_parser
                  parents=[conf_parser],
                  # print script description with -h/--help
                  description=__doc__,
                  formatter_class=argparse.ArgumentDefaultsHelpFormatter)
        parser.set_defaults(**defaults)
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
            "--raw", action="store_true", help="Show raw XML messages")

        parser.add_argument(
            "--object-type", help="Logs of object type", choices=OBJECT_TYPE_LIST)
        parser.add_argument("--object-values", action="store_true",
            help="Display list of object names")
        parser.add_argument("--object-id", help="Logs of object name")
        parser.add_argument(
            "--object-select-field", help="Select field to filter the log",
            choices=[VizConstants.OBJECT_LOG, VizConstants.SYSTEM_LOG])
        parser.add_argument("--trace", help="Dump trace buffer")
        parser.add_argument("--limit", help="Limit the number of messages")
        parser.add_argument("--send-syslog", action="store_true",
                            help="Send syslog to specified server and port")
        parser.add_argument("--syslog-server",
            help="IP address of syslog server", default='localhost')
        parser.add_argument("--syslog-port", help="Port to send syslog to",
            type=int, default=514)
        parser.add_argument("--tail","-f", help="Tail logs from now", action="store_true")
        parser.add_argument("--keywords", help="comma seperated list of keywords")
        parser.add_argument("--message-types", \
         help="Display list of message type", action="store_true")
        parser.add_argument("--output-file", "-o", help="redirect output to file")
        parser.add_argument("--json", help="Dump output as json", action="store_true")
        parser.add_argument("--all", action="store_true", help=argparse.SUPPRESS)        
        self._args = parser.parse_args(remaining_argv)

        self._args.admin_user = args.admin_user
        self._args.admin_password = args.admin_password
        self._args.analytics_api_ip = args.analytics_api_ip
        self._args.analytics_api_port = args.analytics_api_port

        return 0
    # end parse_args

    # Public functions
    def query(self):
        if self._args.tail and (self._args.send_syslog or self._args.reverse or
               self._args.start_time or self._args.end_time):
            invalid_combination = " --tail"
            if self._args.send_syslog:
                 invalid_combination += ", --send-syslog"
            if self._args.reverse:
                 invalid_combination += ", --reverse"
            if self._args.start_time:
                 invalid_combination += ", --start-time"
            if self._args.end_time:
                 invalid_combination += ", --end-time"
            print "Combination of options" + invalid_combination + " are not valid."
            return -1
        global output_file_handle
        if self._args.output_file is not None:
            if output_file_handle is None:
               #Open the file for writing
               try:
                   if self._args.tail:
                      output_file_handle = open(self._args.output_file, "a")
                   else:
                      output_file_handle = open(self._args.output_file, "w")
               except Exception as e:
                   print e
                   print "Exception occured when creating/opening file %s" % \
                         self._args.output_file
                   return -1

        start_time, end_time = self._start_time, self._end_time
        if self._args.message_types is True:
            command_str = ("contrail-stats --table FieldNames.fields" +
               " --where name=MessageTable:Messagetype --select name fields.value" +
               " --start-time " + str(start_time) +
               " --end-time " + str(end_time) +
               " --analytics-api-ip " + str(self._args.analytics_api_ip) +
               " --analytics-api-port " + str(self._args.analytics_api_port))
            res = commands.getoutput(command_str)
            res = res.splitlines()
            res = res[1:]
            for r in res:
                print ast.literal_eval(r)['fields.value']
            return None
        messages_url = OpServerUtils.opserver_query_url(
            self._args.analytics_api_ip,
            self._args.analytics_api_port)
        where_msg = []
        where_obj = []
        and_filter = []
        or_filter = []
        if self._args.source is not None:
            if self._args.source.endswith('*'):
                val = self._args.source[:-1]
                oper = OpServerUtils.MatchOp.PREFIX
            else:
                val = self._args.source
                oper = OpServerUtils.MatchOp.EQUAL
            source_match = OpServerUtils.Match(name=VizConstants.SOURCE,
                                               value=val, op=oper)
            where_msg.append(source_match.__dict__)

        if self._args.module is not None:
            module_match = OpServerUtils.Match(name=VizConstants.MODULE,
                                               value=self._args.module,
                                               op=OpServerUtils.MatchOp.EQUAL)
            where_msg.append(module_match.__dict__)

        if self._args.category is not None:
            if self._args.category.endswith('*'):
                val = self._args.category[:-1]
                oper = OpServerUtils.MatchOp.PREFIX
            else:
                val = self._args.category
                oper = OpServerUtils.MatchOp.EQUAL
            category_match = OpServerUtils.Match(
                name=VizConstants.CATEGORY,
                value=val, op=oper)
            where_msg.append(category_match.__dict__)

        if self._args.message_type is not None:
            if self._args.message_type.endswith('*'):
                val = self._args.message_type[:-1]
                oper = OpServerUtils.MatchOp.PREFIX
            else:
                val = self._args.message_type
                oper = OpServerUtils.MatchOp.EQUAL
            message_type_match = OpServerUtils.Match(
                name=VizConstants.MESSAGE_TYPE,
                value=val, op=oper)
            where_msg.append(message_type_match.__dict__)

        if self._args.level is not None:
            level_match = OpServerUtils.Match(
                name=VizConstants.LEVEL,
                value=SandeshLevel._NAMES_TO_VALUES[self._args.level],
                op=OpServerUtils.MatchOp.LEQ)
            and_filter.append(level_match.__dict__)

        if self._args.node_type is not None:
            node_type_match = OpServerUtils.Match(
                name=VizConstants.NODE_TYPE,
                value=self._args.node_type,
                op=OpServerUtils.MatchOp.EQUAL)
            and_filter.append(node_type_match.__dict__)

        if self._args.instance_id is not None:
            instance_id_match = OpServerUtils.Match(
                name=VizConstants.INSTANCE_ID,
                value=self._args.instance_id,
                op=OpServerUtils.MatchOp.EQUAL)
            and_filter.append(instance_id_match.__dict__)

        # Object logs :
        # --object-type <> : All logs for the particular object type
        # --object-type <> --object-values : Object-id values for the particular
        #     object tye
        # --object-type <> --object-id <> : All logs matching object-id for
        #     particular object type
        if (self._args.object_type is not None or
           self._args.object_id is not None or
           self._args.object_select_field is not None or
           self._args.object_values is True):
            # Validate object-type
            if self._args.object_type is not None:
                if self._args.object_type in OBJECT_TYPE_LIST:
                    if self._args.object_type in OBJECT_TABLE_MAP:
                        table = OBJECT_TABLE_MAP[self._args.object_type]
                    else:
                        print 'Table not found for object-type [%s]' % \
                            (self._args.object_type)
                        return -1
                else:
                    print 'Unknown object-type [%s]' % (self._args.object_type)
                    return -1
            else:
                print 'Object-type required for query'
                return -1
            # Validate object-id and object-values
            if self._args.object_id is not None and \
               self._args.object_values is False:
                object_id = self._args.object_id
                if object_id.endswith("*"):
                    id_match = OpServerUtils.Match(
                        name=OpServerUtils.OBJECT_ID,
                        value=object_id[:-1],
                        op=OpServerUtils.MatchOp.PREFIX) 
                else:
                    id_match = OpServerUtils.Match(
                        name=OpServerUtils.OBJECT_ID,
                        value=object_id,
                        op=OpServerUtils.MatchOp.EQUAL)
                where_obj.append(id_match.__dict__)
            elif self._args.object_id is not None and \
               self._args.object_values is True:
                print 'Please specify either object-id or object-values but not both'
                return -1

            if self._args.object_values is False:
                if self._args.object_select_field is not None:
                    obj_sel_field = self._args.object_select_field
                    if not isinstance(self._args.object_select_field, list):
                         obj_sel_field = [self._args.object_select_field]
                    if VizConstants.OBJECT_LOG or VizConstants.SYSTEM_LOG \
                       in obj_sel_field:
                         self._args.object_select_field = obj_sel_field
                    else:
                         print 'Invalid object-select-field. '\
                            'Valid values are "%s" or "%s"' \
                            % (VizConstants.OBJECT_LOG,
                               VizConstants.SYSTEM_LOG)
                         return -1
                else:
                    self._args.object_select_field = obj_sel_field = [
                        VizConstants.OBJECT_LOG, VizConstants.SYSTEM_LOG]
                select_list = [
                    VizConstants.TIMESTAMP,
                    VizConstants.SOURCE,
                    VizConstants.MODULE,
                    VizConstants.MESSAGE_TYPE,
                ] + obj_sel_field
            else:
                if self._args.object_select_field:
                    print 'Please specify either object-id with ' + \
                        'object-select-field or only object-values'
                    return -1
                if len(where_msg):
                    options = [where['name'] for where in where_msg]
                    print 'Invalid/unsupported where-clause options %s for object-values query' % str(options)
                    return -1
                select_list = [
                    OpServerUtils.OBJECT_ID
                ]

            if len(where_obj) or len(where_msg):
                where = [where_obj + where_msg]
            else:
                where = None

        elif self._args.trace is not None:
            table = VizConstants.COLLECTOR_GLOBAL_TABLE
            if self._args.source is None:
                print 'Source is required for trace buffer dump'
                return -1
            if self._args.module is None:
                print 'Module is required for trace buffer dump'
                return -1
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
                VizConstants.DATA,
                VizConstants.SANDESH_TYPE
            ]
            sandesh_type_filter = OpServerUtils.Match(
                name=VizConstants.SANDESH_TYPE,
                value=str(
                    SandeshType.TRACE),
                op=OpServerUtils.MatchOp.EQUAL)
            and_filter.append(sandesh_type_filter.__dict__)
        else:
            # Message Table Query
            table = VizConstants.COLLECTOR_GLOBAL_TABLE

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

        filter = None
        if len(or_filter):
            filter = [and_filter+[filt] for filt in or_filter]
        elif len(and_filter):
            filter = [and_filter]

        if self._args.keywords is not None:
            p = re.compile('\s*,\s*|\s+')
            if where is None:
                where = [[]]
            for kwd in p.split(self._args.keywords):
                message_type_match = OpServerUtils.Match(
                    name=VizConstants.KEYWORD,
                    value=kwd,
                    op=OpServerUtils.MatchOp.EQUAL)
                for wc in where:
                    wc.append(message_type_match.__dict__)

        # Add sort by timestamp for non object value queries
        sort_op = None
        sort_fields = None
        if self._args.object_values is False:
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
        resp = OpServerUtils.post_url_http(
            messages_url, json.dumps(messages_query.__dict__),
            self._args.admin_user, self._args.admin_password)
        result = {}
        if resp is not None:
            resp = json.loads(resp)
            qid = resp['href'].rsplit('/', 1)[1]
            result = OpServerUtils.get_query_result(
                self._args.analytics_api_ip, self._args.analytics_api_port, qid,
                self._args.admin_user, self._args.admin_password)
        return result
    # end query

    def output(self, log_str, sandesh_level):
        if self._args.json:
             if isinstance(log_str,dict):
                 #convert to json and dump
                 log_str=json.dumps(log_str)
        if self._args.output_file is not None:
            #Append to a file specified
            try:
                output_file_handle.write(log_str)
                output_file_handle.write("\n")
                return
            except Exception as e:
                print e
                print "Exception occured when writing file %s" % \
                      self._args.output_file
                return -1
        if self._args.send_syslog:
            syslog_level = SandeshLogger._SANDESH_LEVEL_TO_LOGGER_LEVEL[
                sandesh_level]
            self._logger.log(syslog_level, log_str)
        else:
            print log_str
    #end output

    def read_result(self, result_gen):
        if not result_gen:
            return
        result_list = []
        for r in result_gen:
            result_list.append(r)
        return result_list
    # end read_result

    def display(self, result):
        if result == [] or result is None:
            return
        messages_dict_list = result
        # Setup logger and syslog handler
        if self._args.send_syslog:
            logger = logging.getLogger()
            logger.setLevel(logging.DEBUG)
            syslog_handler = logging.handlers.SysLogHandler(
                address = (self._args.syslog_server, self._args.syslog_port))
            contrail_formatter = logging.Formatter('contrail: %(message)s')
            syslog_handler.setFormatter(contrail_formatter)
            logger.addHandler(syslog_handler)
            self._logger = logger

        # For json we will be outputting list of dicts so open the list here
        if self._args.json:
            first = True
            self.output('[', SandeshLevel.INVALID)
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
            messages_dict[VizConstants.TIMESTAMP] = message_ts
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
            # By default SYS_DEBUG
            sandesh_level = SandeshLevel.SYS_DEBUG
            if self._args.object_type is None:
                if VizConstants.CATEGORY in messages_dict:
                    category = messages_dict[VizConstants.CATEGORY]
                else:
                    category = 'Category: NA'
                if VizConstants.LEVEL in messages_dict:
                    sandesh_level = messages_dict[VizConstants.LEVEL]
                    level = SandeshLevel._VALUES_TO_NAMES[sandesh_level]
                else:
                    level = 'Level: NA'
                messages_dict[VizConstants.LEVEL] = level
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
                if self._args.json:
                    if not first:
                        self.output(", ", sandesh_level)
                    else:
                        first = False
                    OpServerUtils.messages_dict_scrub(messages_dict)
                    self.output(messages_dict, sandesh_level)
                else:
                    if self._args.trace is not None:
                        trace_str = '{0} {1}:{2} {3}'.format(
                            message_ts, message_type, seq_num, data_str)
                        self.output(trace_str, sandesh_level)
                    else:
                        log_str = \
                            '{0} {1} [{2}:{3}:{4}:{5}][{6}] : {7}:{8} {9}'.format(
                            message_ts, source, node_type, module, instance_id,
                            category, level, message_type, seq_num, data_str)
                        self.output(log_str, sandesh_level)
            else:
                if self._args.object_values is True:
                    if OpServerUtils.OBJECT_ID in messages_dict:
                        obj_str = messages_dict[OpServerUtils.OBJECT_ID]
                        print obj_str
                        continue
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
                            obj_str = '{0} {1} [{2}:{3}:{4}] : {5}: {6}'.format(
                                message_ts, source, node_type, module,
                                instance_id, message_type, data_str)
                            if self._args.json:
                                if not first:
                                    self.output(", ", sandesh_level)
                                else:
                                    first = False
                                OpServerUtils.messages_dict_scrub(messages_dict)
                                self.output(messages_dict, sandesh_level)
                            else:
                                self.output(obj_str, sandesh_level)
        # For json we will be outputting list of dicts so close the list here
        if self._args.json:
            self.output(']', SandeshLevel.INVALID)
    # end display

# end class LogQuerier


def main():
    querier = LogQuerier()
    querier.run()
# end main

if __name__ == "__main__":
    main()
