#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

from builtins import str
import re

from cfgm_common.exceptions import NoIdError
from cfgm_common.exceptions import VncError
from vnc_api.gen.resource_common import RoutingPolicy

from vnc_cfg_api_server.resources._resource_base import ResourceMixin


class RoutingPolicyServer(ResourceMixin, RoutingPolicy):

    @staticmethod
    def _check_routefilter_range_string(rfvalue):
        """Validate route_type_value of route_filter.

        Value must be in the format of "/n-/n" where n must be the number
        from 1 to 32.
        : Args:
        : rfvalue: route_type_value of route_filter property
        : return:
        : returns True if value is in expected proper format else returns
        : False with proper error message
        :
        """
        res = None
        if rfvalue:
            res = re.match(
                r'^\/([1-9]|[1-2]\d|3[0-2])-\/([1-9]|[1-2]\d|3[0-2])$',
                rfvalue)
        if not res:
            msg = ("route-filter value %s invalid. prefix-length-range "
                   "should be in format '/minlength-/maxlength'" % rfvalue)
            return False, (400, msg)
        return True, ''
    # end _check_routefilter_range_string

    @staticmethod
    def _check_aspath_string(aspath, aspath_type):
        """Validate as_path_expand and as_path_prepend value.

        value must be in the format of "n" or "n n" where n is integer number
        represents ASN number.
        : Args:
        : aspath: value of as_path_expand or as_path_prepend
        : aspath_type: either as_path_expand or as_path_prepend
        : return:
        : returns True if value is in expected proper format else returns
        : False with proper error message
        :
        """
        if aspath:
            res = re.match(r'^\d+( \d+)*$', aspath)
            if not res:
                msg = ("%s has invalid value %s. valid format is "
                       "AS number(s) seperated by spaces" %
                       (aspath_type, aspath))
                return False, (400, msg)
        return True, ''
    # end _check_aspath_string

    @staticmethod
    def _check_rp_term_route_filter(cls, route_filter):
        """Validate network-device type term's route_filter.

        It validates route_filter defined in term_match_condition.
        : Args:
        : route_filter: route_filter from term_match_condition
        : return:
        : returns True if value is in expected proper format else returns
        : False with proper error message
        :
        """
        if not route_filter:
            return True, ""
        for route_filter in route_filter.get(
                'route_filter_properties') or []:
            if route_filter.get('route') is None:
                msg = ('route_filter must have non-empty route value '
                       'for network-device term')
                return False, (400, msg)
            route_type = route_filter.get('route_type')
            if route_type is None:
                msg = ('route_filter must have valid route_type '
                       'for network-device term')
                return False, (400, msg)
            route_tvalue = route_filter.get('route_type_value')
            if route_type == 'prefix-length-range':
                ok, result = cls._check_routefilter_range_string(
                    route_tvalue)
                if not ok:
                    return ok, result
            elif route_type == 'through' or route_type == 'upto':
                res = None
                if route_tvalue:
                    if route_type == 'upto':
                        # upto value should be in format of "/number"
                        # where allowed number is from 1 to 32
                        res = re.match(
                            r'^\/([1-9]|[1-2]\d|3[0-2])$',
                            route_tvalue)
                    else:
                        if len(route_tvalue) > 0:
                            res = True
                if not res:
                    if route_type == 'upto':
                        msg = ("route-filter type value '%s' is"
                               " invalid. '%s' should be in "
                               "format '/number'" % (route_tvalue,
                                                     route_type))
                    else:
                        msg = ("route-filter type value for "
                               "'%s' should be non-empty "
                               "string." % route_type)
                    return False, (400, msg)
        return True, ""
    # end _check_rp_term_route_filter

    @staticmethod
    def _check_rp_term_match_condition(cls, match_condition, db_conn):
        """Validate network-device type term's term_match_condition.

        It validates protocols, nlri_route_type, prefix_list and
        route_filter defined in term_match_condition.
        : Args:
        : match_condition: term's term_match_condition
        : db_conn: db connection handler
        : return:
        : returns True if value is in expected proper format else returns
        : False with proper error message
        :
        """
        if not match_condition:
            return True, ""
        protocol = match_condition.get('protocol')
        if protocol:
            allowed_protocol = set(['bgp', 'static', 'aggregate', 'pim',
                                    'direct', 'evpn', 'ospf', 'ospf3'])
            for p in protocol:
                if p not in allowed_protocol:
                    msg = ('%s protocol value is not valid value for '
                           'network-device term' % p)
                    return False, (400, msg)
        nlri_route_type = match_condition.get('nlri_route_type')
        for nlri in nlri_route_type or []:
            if nlri < 1 or nlri > 10:
                msg = ('%s nlri-route-type is not valid value, '
                       'value should be in range of 1 to 10' %
                       nlri)
                return False, (400, msg)
        prefix_list = match_condition.get('prefix_list')
        for prefix in prefix_list or []:
            irt_list = prefix.get('interface_route_table_uuid')
            for irt in irt_list or []:
                ok, read_result = cls.dbe_read(
                    db_conn, 'interface_route_table', irt)
                if not ok:
                    return ok, read_result

        route_filter_type = match_condition.get('route_filter')
        return cls._check_rp_term_route_filter(cls, route_filter_type)
    # end _check_rp_term_match_condition

    @staticmethod
    def _check_network_device_routing_policy_term(cls, term, term_type,
                                                  db_conn):
        """Validate network-device type term.

        It validates term's various properties and its values should be in
        expected format as per junos device.
        : Args:
        : term: term to be validated
        : term_type: either network-device or vrouter
        : db_conn: db connection handler
        : return:
        : returns True if value is in expected proper format else returns
        : False with proper error message
        :
        """
        if not term or not term_type or term_type != 'network-device':
            return True, ''
        match_condition = term.get('term_match_condition')
        ok, result = cls._check_rp_term_match_condition(cls,
                                                        match_condition,
                                                        db_conn)
        if not ok:
            return ok, result

        action_list = term.get('term_action_list')
        if action_list:
            aspath_e = action_list.get('as_path_expand')
            ok, result = cls._check_aspath_string(aspath_e, 'as_path_expand')
            if not ok:
                return ok, result
            aspath_e = action_list.get('as_path_prepend')
            ok, result = cls._check_aspath_string(aspath_e, 'as_path_prepend')
            if not ok:
                return ok, result

        return True, ''
    # end _check_network_device_routing_policy_term

    @staticmethod
    def _validate_term_and_get_asnlist(cls, obj_dict, db_conn):
        asn_list = []
        rp_entries = obj_dict.get('routing_policy_entries')
        term_type = obj_dict.get('term_type', 'vrouter')
        if rp_entries:
            termlist = rp_entries.get('term') or []
            if term_type != 'network-device':
                if len(termlist) > 1:
                    termlist = [termlist[0]]
            for term in termlist:
                # validate network-device type term,
                ok, result = cls._check_network_device_routing_policy_term(
                    cls, term, term_type, db_conn)
                if not ok:
                    return False, asn_list, result
                action_list = term.get('term_action_list')
                if action_list:
                    action = action_list.get('update')
                    if action:
                        as_path = action.get('as_path')
                        if as_path:
                            expand = as_path.get('expand')
                            if expand:
                                tasn_list = expand.get('asn_list')
                                if tasn_list:
                                    asn_list.extend(tasn_list)
        return True, asn_list, ''
    # end _validate_term_and_get_asnlist

    @staticmethod
    def _update_irt_refs_pre_dbe(cls, obj_dict, db_conn):
        """Update routing policy refs to interface route table.

        This function adds or removes routing policy refs to interface route
        table based on irt exist in term_match_condition->prefix_list's
        interface_route_table_uuid property.
        Add or remove Refs for rp to irt in current obj_dict object only. this
        way at pre dbe operation time only same obj_dict gets updated with
        proper refs. This is optmized way and gives better scale performance
        to handle ref updates causes it does not creates extra db update calls
        for refs update which saves vnc change notification cycles in DM also.
        : Args:
        : cls: current class
        : obj_dict: routing policy db object
        : db_conn: db connection handler
        : return:
        : returns nothing.
        :
        """
        rp_entries = obj_dict.get('routing_policy_entries')
        if not rp_entries:
            return
        irt_uuid_list = set()
        # scan and find all irt uuid exists
        for term in rp_entries.get('term') or []:
            match_condition = term.get('term_match_condition')
            if not match_condition:
                continue
            prefix_list = match_condition.get('prefix_list')
            for prefix in prefix_list or []:
                irt_list = prefix.get('interface_route_table_uuid', [])
                irt_uuid_list.update(irt_list)

        # create new irt_refs keys inside current obj dict and
        # add all valid irt_uuid in this refs.
        # Note: if irt_uuid removed then this irt_ref will not be added so
        # it will get removed from db also.

        # Ignore error during updating ref as its not a fatal and next update
        # will retry failure ref update. DM module handles this scenerio.
        irt_refs = []
        for irt_uuid in irt_uuid_list:
            try:
                ref_fq_name = db_conn.uuid_to_fq_name(irt_uuid)
            except NoIdError:
                continue
            irt_refs.append(
                {'to': ref_fq_name, 'attr': None, 'uuid': irt_uuid})

        obj_dict['interface_route_table_refs'] = irt_refs
    # end _update_irt_refs_pre_dbe

    @classmethod
    def pre_dbe_create(cls, tenant_name, obj_dict, db_conn):
        asn_list = None
        try:
            ok, asn_list, result = cls._validate_term_and_get_asnlist(
                cls, obj_dict, db_conn)
            if not ok:
                return False, result
        except Exception as e:
            return False, (400, str(e))

        try:
            global_asn = cls.server.global_autonomous_system
        except VncError as e:
            return False, (400, str(e))

        if asn_list and global_asn in asn_list:
            msg = ("ASN can't be same as global system config asn")
            return False, (400, msg)

        cls._update_irt_refs_pre_dbe(cls, obj_dict, db_conn)
        return True, ""
    # end pre_dbe_create

    @classmethod
    def pre_dbe_update(cls, id, fq_name, obj_dict, db_conn, **kwargs):
        ok, read_result = cls.dbe_read(db_conn, 'routing_policy', id,
                                       obj_fields=['routing_policy_entries',
                                                   'term_type'])
        if not ok:
            return ok, read_result
        # do not allow change in term type
        old_term_type = read_result.get('term_type')
        new_term_type = obj_dict.get('term_type')
        if old_term_type and new_term_type:
            if old_term_type != new_term_type:
                return False, (
                    403, "Cannot change term_type of routing policy! "
                         "Please specify '%s' as a term_type in "
                         "routing_policy_entries." % old_term_type)
        try:
            ok, _, result = cls._validate_term_and_get_asnlist(
                cls, obj_dict, db_conn)
            if not ok:
                return False, result
        except Exception as e:
            return False, (400, str(e))

        cls._update_irt_refs_pre_dbe(cls, obj_dict, db_conn)
        return True, ""
    # end pre_dbe_update
