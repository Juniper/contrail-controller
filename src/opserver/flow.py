#!/usr/bin/python

#
# Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
#

#
# flow
#
# Query flow messages from analytics
#

import sys
import argparse
import json
import datetime
import sandesh.viz.constants as VizConstants
from sandesh.viz.ttypes import FlowRecordFields
from opserver_util import OpServerUtils

class FlowQuerier(object):

    def __init__(self):
        self._args = None
        self._VROUTER = VizConstants.FlowRecordNames[
            FlowRecordFields.FLOWREC_VROUTER]
        self._SETUP_TIME = VizConstants.FlowRecordNames[
            FlowRecordFields.FLOWREC_SETUP_TIME]
        self._TEARDOWN_TIME = VizConstants.FlowRecordNames[
            FlowRecordFields.FLOWREC_TEARDOWN_TIME]
        self._SOURCE_VN = VizConstants.FlowRecordNames[
            FlowRecordFields.FLOWREC_SOURCEVN]
        self._DESTINATION_VN = VizConstants.FlowRecordNames[
            FlowRecordFields.FLOWREC_DESTVN]
        self._SOURCE_IP = VizConstants.FlowRecordNames[
            FlowRecordFields.FLOWREC_SOURCEIP]
        self._DESTINATION_IP = VizConstants.FlowRecordNames[
            FlowRecordFields.FLOWREC_DESTIP]
        self._PROTOCOL = VizConstants.FlowRecordNames[
            FlowRecordFields.FLOWREC_PROTOCOL]
        self._SOURCE_PORT = VizConstants.FlowRecordNames[
            FlowRecordFields.FLOWREC_SPORT]
        self._DESTINATION_PORT = VizConstants.FlowRecordNames[
            FlowRecordFields.FLOWREC_DPORT]
        self._DIRECTION = VizConstants.FlowRecordNames[
            FlowRecordFields.FLOWREC_DIRECTION_ING]
        self._ACTION = VizConstants.FlowRecordNames[
            FlowRecordFields.FLOWREC_ACTION]
        self._SG_RULE_UUID = VizConstants.FlowRecordNames[
            FlowRecordFields.FLOWREC_SG_RULE_UUID]
        self._NW_ACE_UUID = VizConstants.FlowRecordNames[
            FlowRecordFields.FLOWREC_NW_ACE_UUID]
    # end __init__

    # Public functions
    def parse_args(self):
        """
        Eg. python flow.py --opserver-ip 127.0.0.1
                          --opserver-port 8081
                          --vrouter a6s23
                          --source-vn default-domain:default-project:vn1
                          --destination-vn default-domain:default-project:vn2
                          --source-ip 1.1.1.1
                          --destination-ip 2.2.2.2
                          --protocol TCP
                          --source-port 32678
                          --destination-port 80
                          --action drop
                          --direction ingress
                          [--start-time now-10m --end-time now] | --last 10m
        """
        defaults = {
            'opserver_ip': '127.0.0.1',
            'opserver_port': '8081',
            'start_time': 'now-10m',
            'end_time': 'now',
            'direction' : 'ingress',
        }

        parser = argparse.ArgumentParser(
            formatter_class=argparse.ArgumentDefaultsHelpFormatter)
        parser.set_defaults(**defaults)
        parser.add_argument("--opserver-ip", help="IP address of OpServer")
        parser.add_argument("--opserver-port", help="Port of OpServer")
        parser.add_argument(
            "--start-time", help="Flow record start time (format now-10m, now-1h)")
        parser.add_argument("--end-time", help="Flow record end time")
        parser.add_argument(
            "--last", help="Flow records from last time period (format 10m, 1d)")
        parser.add_argument("--vrouter", help="Flow records from vrouter")
        parser.add_argument("--source-vn",
            help="Flow records with source virtual network")
        parser.add_argument("--destination-vn",
            help="Flow records with destination virtual network")
        parser.add_argument("--source-ip",
            help="Flow records with source IP address")
        parser.add_argument("--destination-ip",
            help="Flow records with destination IP address")
        parser.add_argument("--protocol", help="Flow records with protocol")
        parser.add_argument("--source-port",
            help="Flow records with source port", type=int)
        parser.add_argument("--destination-port",
            help="Flow records with destination port", type=int)
        parser.add_argument("--action", help="Flow records with action")
        parser.add_argument("--direction", help="Flow direction",
            choices=['ingress', 'egress'])
        parser.add_argument(
            "--verbose", action="store_true", help="Show internal information")        
        self._args = parser.parse_args()

        try:
            self._start_time, self._end_time = \
                OpServerUtils.parse_start_end_time(
                    start_time = self._args.start_time,
                    end_time = self._args.end_time,
                    last = self._args.last)
        except:
            return -1

        # Validate flow arguments
        if self._args.source_ip is not None and self._args.source_vn is None:
            print 'Please provide source virtual network in addtion to '\
                'source IP address'
            return -1
        if self._args.destination_ip is not None and \
                self._args.destination_vn is None:
            print 'Please provide destination virtual network in addtion to '\
                'destination IP address'
            return -1
        if self._args.source_port is not None and self._args.protocol is None:
            print 'Please provide protocol in addtion to source port'
            return -1
        if self._args.destination_port is not None and \
                self._args.protocol is None:
            print 'Please provide protocol in addtion to '\
                'destination port'
            return -1

        # Convert direction
        if self._args.direction.lower() == "ingress":
            self._args.direction = 1
        elif self._args.direction.lower() == "egress":
            self._args.direction = 0
        else:
            print 'Direction should be ingress or egress'
            return -1

        # Protocol
        if self._args.protocol is not None:
            if self._args.protocol.isalpha():
                protocol = OpServerUtils.str_to_ip_protocol(
                    self._args.protocol)
                if protocol == -1:
                    print 'Please provide valid protocol'
                    return -1
                self._args.protocol = protocol

        return 0
    # end parse_args

    # Public functions
    def query(self):
        start_time, end_time = self._start_time, self._end_time
        flow_url = OpServerUtils.opserver_query_url(
            self._args.opserver_ip,
            self._args.opserver_port)
        where = []
        filter = []
        if self._args.vrouter is not None:
            vrouter_match = OpServerUtils.Match(
                name=self._VROUTER,
                value=self._args.vrouter,
                op=OpServerUtils.MatchOp.EQUAL)
            where.append(vrouter_match.__dict__)

        if self._args.source_vn is not None:
            source_vn_match = OpServerUtils.Match(
                name=self._SOURCE_VN,
                value=self._args.source_vn,
                op=OpServerUtils.MatchOp.EQUAL)
            where.append(source_vn_match.__dict__)

        if self._args.destination_vn is not None:
            dest_vn_match = OpServerUtils.Match(
                name=self._DESTINATION_VN,
                value=self._args.destination_vn,
                op=OpServerUtils.MatchOp.EQUAL)
            where.append(dest_vn_match.__dict__)

        if self._args.source_ip is not None:
            source_ip_match = OpServerUtils.Match(
                name=self._SOURCE_IP,
                value=self._args.source_ip,
                op=OpServerUtils.MatchOp.EQUAL)
            where.append(source_ip_match.__dict__)

        if self._args.destination_ip is not None:
            dest_ip_match = OpServerUtils.Match(
                name=self._DESTINATION_IP,
                value=self._args.destination_ip,
                op=OpServerUtils.MatchOp.EQUAL)
            where.append(dest_ip_match.__dict__)

        if self._args.protocol is not None:
            protocol_match = OpServerUtils.Match(
                name=self._PROTOCOL,
                value=self._args.protocol,
                op=OpServerUtils.MatchOp.EQUAL)
            where.append(protocol_match.__dict__)

        if self._args.source_port is not None:
            source_port_match = OpServerUtils.Match(
                name=self._SOURCE_PORT,
                value=self._args.source_port,
                op=OpServerUtils.MatchOp.EQUAL)
            where.append(source_port_match.__dict__)

        if self._args.destination_port is not None:
            dest_port_match = OpServerUtils.Match(
                name=self._DESTINATION_PORT,
                value=self._args.destination_port,
                op=OpServerUtils.MatchOp.EQUAL)
            where.append(dest_port_match.__dict__)

        if self._args.action is not None:
            action_match = OpServerUtils.Match(
                name=self._ACTION,
                value=self._args.action,
                op=OpServerUtils.MatchOp.EQUAL)
            filter.append(action_match.__dict__)

        # Flow Record Table Query
        table = VizConstants.FLOW_TABLE
        if len(where) == 0:
            where = None
        else:
            where = [where]

        select_list = [
            VizConstants.FLOW_TABLE_UUID,
            self._VROUTER,
            self._SETUP_TIME,
            self._TEARDOWN_TIME,
            self._SOURCE_VN,
            self._DESTINATION_VN,
            self._SOURCE_IP,
            self._DESTINATION_IP,
            self._PROTOCOL,
            self._SOURCE_PORT,
            self._DESTINATION_PORT,
            self._ACTION,
            self._DIRECTION,
            VizConstants.FLOW_TABLE_AGG_BYTES,
            VizConstants.FLOW_TABLE_AGG_PKTS,
            self._SG_RULE_UUID,
            self._NW_ACE_UUID,
        ]

        if len(filter) == 0:
            filter = None

        flow_query = OpServerUtils.Query(table,
                                         start_time=start_time,
                                         end_time=end_time,
                                         select_fields=select_list,
                                         where=where,
                                         filter=filter,
                                         dir=self._args.direction)
        if self._args.verbose:
            print 'Performing query: {0}'.format(
                json.dumps(flow_query.__dict__))
        print ''
        resp = OpServerUtils.post_url_http(
            flow_url, json.dumps(flow_query.__dict__))
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
        flow_dict_list = result
    
        for flow_dict in flow_dict_list:
            # Setup time
            if self._SETUP_TIME in flow_dict:
                setup_time = int(flow_dict[self._SETUP_TIME])
                if setup_time != 0:
                    setup_dt = datetime.datetime.fromtimestamp(
                        setup_time /
                        OpServerUtils.USECS_IN_SEC)
                    setup_dt += datetime.timedelta(
                        microseconds=
                        (setup_time %
                         OpServerUtils.USECS_IN_SEC))
                    setup_ts = setup_dt.strftime(
                        OpServerUtils.TIME_FORMAT_STR)
                else:
                    setup_ts = 'Setup Time: NA'
            else:
                setup_ts = 'Setup Time: NA'
            # Teardown time
            if self._TEARDOWN_TIME in flow_dict:
                teardown_time = int(flow_dict[ 
                    self._TEARDOWN_TIME])
                if teardown_time != 0:
                    teardown_dt = datetime.datetime.fromtimestamp(
                        teardown_time /
                        OpServerUtils.USECS_IN_SEC)
                    teardown_dt += datetime.timedelta(
                        microseconds=
                        (teardown_time %
                         OpServerUtils.USECS_IN_SEC))
                    teardown_ts = teardown_dt.strftime(
                        OpServerUtils.TIME_FORMAT_STR)
                else:
                    teardown_ts = 'Active'
            else:
                teardown_ts = 'Active'
            # VRouter
            if self._VROUTER in flow_dict:
                vrouter = flow_dict[self._VROUTER]
            else:
                vrouter = 'VRouter: NA'
            # Direction 
            if self._DIRECTION in flow_dict:
                direction = int(flow_dict[self._DIRECTION])
                if direction == 1:
                    direction = 'ingress'
                elif direction == 0:
                    direction = 'egress'
                else:
                    direction = 'Direction: Invalid'
            else:
                direction = 'Direction: NA'
            # Flow UUID 
            if VizConstants.FLOW_TABLE_UUID in flow_dict:
                flow_uuid = flow_dict[VizConstants.FLOW_TABLE_UUID]
            else:
                flow_uuid = 'UUID: NA'
            # Source VN
            if self._SOURCE_VN in flow_dict:
                source_vn = flow_dict[self._SOURCE_VN]
            else:
                source_vn = 'Source VN: NA'
            # Destination VN
            if self._DESTINATION_VN in flow_dict:
                destination_vn = flow_dict[self._DESTINATION_VN]
            else:
                destination_vn = 'Destination VN: NA'
            # Source IP 
            if self._SOURCE_IP in flow_dict:
                source_ip = flow_dict[self._SOURCE_IP]
            else:
                source_ip = 'Source IP: NA'
            # Destination IP 
            if self._DESTINATION_IP in flow_dict:
                destination_ip = flow_dict[self._DESTINATION_IP]
            else:
                destination_ip = 'Destination IP: NA'
            # Source port 
            if self._SOURCE_PORT in flow_dict:
                source_port = flow_dict[self._SOURCE_PORT]
            else:
                source_port = 'Source Port: NA'
            # Destination port 
            if self._DESTINATION_PORT in flow_dict:
                destination_port = flow_dict[self._DESTINATION_PORT]
            else:
                destination_port = 'Destination Port: NA'
            # Protocol
            if self._PROTOCOL in flow_dict:
                protocol = OpServerUtils.ip_protocol_to_str(
                    int(flow_dict[self._PROTOCOL]))
            else:
                protocol = 'Protocol: NA'
            # Action 
            if self._ACTION in flow_dict:
                action = flow_dict[self._ACTION]
            else:
                action = ''
            # Agg packets and bytes
            if VizConstants.FLOW_TABLE_AGG_BYTES in flow_dict:
                agg_bytes = int(flow_dict[VizConstants.FLOW_TABLE_AGG_BYTES])
            else:
                agg_bytes = 'Agg Bytes: NA'
            if VizConstants.FLOW_TABLE_AGG_PKTS in flow_dict:
                agg_pkts = int(flow_dict[VizConstants.FLOW_TABLE_AGG_PKTS])
            else:
                agg_pkts = 'Agg Packets: NA'
            # SG rule UUID
            if self._SG_RULE_UUID in flow_dict:
                sg_rule_uuid = flow_dict[self._SG_RULE_UUID]
            else:
                sg_rule_uuid = None
            # NW ACE UUID
            if self._NW_ACE_UUID in flow_dict:
                nw_ace_uuid = flow_dict[self._NW_ACE_UUID]
            else:
                nw_ace_uuid = None
            print '{0}({1}) {2} [{3} -- {4}] {5} '\
                '{6}:{7}:{8} ---> {9}:{10}:{11} [{12} P ({13} B)]'\
                ' : SG:{14} ACL:{15} {16}'.format(
               vrouter, direction, action, setup_ts, teardown_ts,
               protocol, source_vn, source_ip, source_port, destination_vn,
               destination_ip, destination_port, agg_pkts, agg_bytes,
               sg_rule_uuid, nw_ace_uuid, flow_uuid)
    # end display

# end class FlowQuerier


def main():
    querier = FlowQuerier()
    if querier.parse_args() != 0:
        return
    result = querier.query()
    querier.display(result)
# end main

if __name__ == "__main__":
    main()
