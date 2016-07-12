#
# Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
#

#
# Overlay To Underlay Mapper
#
# Utility to get the Underlay information for the Overlay flow(s).
#

import json

from sandesh.viz.constants import *
from opserver_util import OpServerUtils


class OverlayToUnderlayMapperError(Exception):
    """Base Exception class for this module.

    All the Exceptions defined in this module should be derived from
    this class. The application/module that calls any method in the
    OverlayToUnderlayMapper class should catch this base Exception.
    """
    pass


class OverlayToUnderlayMapper(object):

    def __init__(self, query_json, analytics_api_ip,
                 analytics_api_port, user, password, logger):
        self.query_json = query_json
        self._analytics_api_ip = analytics_api_ip
        self._analytics_api_port = analytics_api_port
        self._user = user
        self._password = password
        self._logger = logger
        if self.query_json is not None:
            self._start_time = self.query_json['start_time']
            self._end_time = self.query_json['end_time']
            # If the start_time/end_time in the query is specified as
            # relative time, then the actual start_time/end_time for the
            # FlowRecordTable query and UFlowData query would be different.
            # Since the FlowRecordTable is queried first and the result of
            # which is used to query the UFlowData table, the result may
            # not be correct if the start_time/end_time is different for
            # FlowRecord and UFlowData queries. Therefore, convert the
            # relative start/end time to absolute time.
            if not str(self._start_time).isdigit():
                self._start_time = \
                    OpServerUtils.convert_to_utc_timestamp_usec(self._start_time)
            if not str(self._end_time).isdigit():
                self._end_time = \
                    OpServerUtils.convert_to_utc_timestamp_usec(self._end_time)
    # end __init__

    def process_query(self):
        """Process the OverlayToUnderlay Flow query and returns
        the response."""
        flow_record_data = self._get_overlay_flow_data()
        uflow_data = self._get_underlay_flow_data(flow_record_data)
        return self._send_response(uflow_data)
    # end process_query

    def _overlay_to_flowrecord_name(self, oname):
        try:
            fname = OverlayToFlowRecordFields[oname]
        except KeyError:
            raise _OverlayToFlowRecordFieldsNameError(oname)
        return fname
    # end _overlay_to_flowrecord_name

    def _flowrecord_to_uflowdata_name(self, fname):
        try:
            ufname = FlowRecordToUFlowDataFields[fname]
        except KeyError:
            raise _FlowRecordToUFlowDataFieldsNameError(fname)
        return ufname
    # end _flowrecord_to_uflowdata_name

    def _underlay_to_uflowdata_name(self, uname):
        try:
            ufname = UnderlayToUFlowDataFields[uname]
        except KeyError:
            raise _UnderlayToUFlowDataFieldsNameError(uname)
        return ufname
    # end _underlay_to_uflowdata_name

    def _get_overlay_flow_data(self):
        """Fetch the overlay flow data from the FlowRecord Table.

        Convert the where clause in the OverlayToUnderlay query according
        to the schema defined for the FlowRecord Table. Get the overlay
        flow data [source vrouter, destination vrouter, flowtuple hash,
        encapsulation] from the FlowRecord Table required to query the
        underlay data.
        """
        # process where clause
        try:
            where_or_list = self.query_json['where']
        except KeyError:
            where_or_list = []
        flow_record_where = []
        for where_and_list in where_or_list:
            flow_record_where_and_list = []
            for match_term in where_and_list:
                fname = self._overlay_to_flowrecord_name(match_term['name'])
                match = OpServerUtils.Match(name=fname,
                            value=match_term['value'],
                            op=match_term['op'],
                            value2=match_term.get('value2'))
                flow_record_where_and_list.append(match.__dict__)
                if match_term.get('suffix') is not None:
                    fname = self._overlay_to_flowrecord_name(
                                match_term['suffix']['name'])
                    match = OpServerUtils.Match(name=fname,
                                value=match_term['suffix']['value'],
                                op=match_term['suffix']['op'],
                                value2=match_term['suffix'].get('value2'))
                    flow_record_where_and_list.append(match.__dict__)
            flow_record_where.append(flow_record_where_and_list)

        # populate the select list
        flow_record_select = [
            FlowRecordNames[FlowRecordFields.FLOWREC_VROUTER_IP],
            FlowRecordNames[FlowRecordFields.FLOWREC_OTHER_VROUTER_IP],
            FlowRecordNames[FlowRecordFields.FLOWREC_UNDERLAY_SPORT],
            FlowRecordNames[FlowRecordFields.FLOWREC_UNDERLAY_PROTO]
        ]

        flow_record_query = OpServerUtils.Query(table=FLOW_TABLE,
                                start_time=self._start_time,
                                end_time=self._end_time,
                                select_fields=flow_record_select,
                                where=flow_record_where,
                                dir=1)
        return self._send_query(json.dumps(flow_record_query.__dict__))
    # end _get_overlay_flow_data

    def _get_underlay_flow_data(self, flow_record_data):
        """Fetch the underlay data from the UFlowData table.

        Construct the Where clause for the UFlowData query from the
        FlowRecord query response. Convert the select clause, sort_fields,
        filter clause in the OverlayToUnderlay query according to the schema
        defined for the UFlowData table.
        """
        if not len(flow_record_data):
            return []

        # populate where clause for Underlay Flow query
        uflow_data_where = []
        for row in flow_record_data:
            # if any of the column value is None, then skip the row
            if any(col == None for col in row.values()):
                continue
            uflow_data_where_and_list = []
            ufname = self._flowrecord_to_uflowdata_name(
                FlowRecordNames[FlowRecordFields.FLOWREC_VROUTER_IP])
            val = row[FlowRecordNames[FlowRecordFields.FLOWREC_VROUTER_IP]]
            sip = OpServerUtils.Match(name=ufname, value=val,
                op=OpServerUtils.MatchOp.EQUAL)
            uflow_data_where_and_list.append(sip.__dict__)
            ufname = self._flowrecord_to_uflowdata_name(
                FlowRecordNames[FlowRecordFields.FLOWREC_OTHER_VROUTER_IP])
            val = \
                row[FlowRecordNames[FlowRecordFields.FLOWREC_OTHER_VROUTER_IP]]
            dip = OpServerUtils.Match(name=ufname, value=val,
                op=OpServerUtils.MatchOp.EQUAL)
            uflow_data_where_and_list.append(dip.__dict__)
            ufname = self._flowrecord_to_uflowdata_name(
                FlowRecordNames[FlowRecordFields.FLOWREC_UNDERLAY_SPORT])
            val = row[FlowRecordNames[FlowRecordFields.FLOWREC_UNDERLAY_SPORT]]
            sport = OpServerUtils.Match(name=ufname, value=val,
                    op=OpServerUtils.MatchOp.EQUAL)
            ufname = self._flowrecord_to_uflowdata_name(
                    FlowRecordNames[FlowRecordFields.FLOWREC_UNDERLAY_PROTO])
            val = row[FlowRecordNames[FlowRecordFields.FLOWREC_UNDERLAY_PROTO]]
            # get the protocol from tunnel_type
            val = OpServerUtils.tunnel_type_to_protocol(val)
            protocol = OpServerUtils.Match(name=ufname, value=val,
                    op=OpServerUtils.MatchOp.EQUAL, suffix=sport)
            uflow_data_where_and_list.append(protocol.__dict__)
            uflow_data_where.append(uflow_data_where_and_list)

        # if the where clause is empty, then no need to send
        # the UFlowData query
        if not len(uflow_data_where):
            return []

        # populate UFlowData select
        uflow_data_select = []
        for select in self.query_json['select_fields']:
            uflow_data_select.append(self._underlay_to_uflowdata_name(select))

        # sort_fields specified in the query?
        uflow_data_sort_fields = None
        if self.query_json.get('sort_fields'):
            uflow_data_sort_fields = []
            for field in self.query_json['sort_fields']:
                uflow_data_sort_fields.append(
                    self._underlay_to_uflowdata_name(field))
        uflow_data_sort_type = self.query_json.get('sort')

        # does the query contain limit attribute?
        uflow_data_limit = self.query_json.get('limit')

        # add filter if specified
        uflow_data_filter = None
        if self.query_json.get('filter') is not None:
            uflow_data_filter = list(self.query_json['filter'])
            if len(uflow_data_filter):
                if not isinstance(uflow_data_filter[0], list):
                    uflow_data_filter = [uflow_data_filter]
            for filter_and in uflow_data_filter:
                for match_term in filter_and:
                    match_term['name'] = self._underlay_to_uflowdata_name(
                                            match_term['name'])

        uflow_data_query = OpServerUtils.Query(
                                table='StatTable.UFlowData.flow',
                                start_time=self._start_time,
                                end_time=self._end_time,
                                select_fields=uflow_data_select,
                                where=uflow_data_where,
                                sort=uflow_data_sort_type,
                                sort_fields=uflow_data_sort_fields,
                                limit=uflow_data_limit,
                                filter=uflow_data_filter)
        return self._send_query(json.dumps(uflow_data_query.__dict__))
    # end _get_underlay_flow_data

    def _send_query(self, query):
        """Post the query to the analytics-api server and returns the
        response."""
        self._logger.debug('Sending query: %s' % (query))
        opserver_url = OpServerUtils.opserver_query_url(self._analytics_api_ip,
                           str(self._analytics_api_port))
        resp = OpServerUtils.post_url_http(opserver_url, query, self._user,
            self._password, True)
        try:
            resp = json.loads(resp)
            value = resp['value']
        except (TypeError, ValueError, KeyError):
            raise _QueryError(query)
        self._logger.debug('Query response: %s' % str(value))
        return value
    # end _send_query

    def _send_response(self, uflow_data):
        """Converts the UFlowData query response according to the
        schema defined for the OverlayToUnderlayFlowMap table."""
        underlay_response = {}
        underlay_data = []
        for row in uflow_data:
            underlay_row = {}
            for field in self.query_json['select_fields']:
                name = self._underlay_to_uflowdata_name(field)
                underlay_row[field] = row[name]
            underlay_data.append(underlay_row)
        underlay_response['value'] = underlay_data
        return json.dumps(underlay_response)
    # end _send_response

# end class OverlayToUnderlayMapper


class _OverlayToFlowRecordFieldsNameError(OverlayToUnderlayMapperError):
    def __init__(self, field):
        self.field = field

    def __str__(self):
        return 'No mapping for <%s> in "OverlayToFlowRecordFields"' \
            % (self.field)
# end class _OverlayToFlowRecordFieldsNameError


class _FlowRecordToUFlowDataFieldsNameError(OverlayToUnderlayMapperError):
    def __init__(self, field):
        self.field = field

    def __str__(self):
        return 'No mapping for <%s> in "FlowRecordToUFlowDataFields"' \
            % (self.field)
# end class _FlowRecordToUFlowDataFieldsNameError


class _UnderlayToUFlowDataFieldsNameError(OverlayToUnderlayMapperError):
    def __init__(self, field):
        self.field = field

    def __str__(self):
        return 'No mapping for <%s> in "UnderlayToUFlowDataFields"' \
            % (self.field)
# end class _UnderlayToUFlowDataFieldsNameError


class _QueryError(OverlayToUnderlayMapperError):
    def __init__(self, query):
        self.query = query

    def __str__(self):
        return 'Error in query processing: %s' % (self.query)
# end class _QueryError
