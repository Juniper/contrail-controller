#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

try:
    # Python 2
    from __builtin__ import str
except ImportError:
    # Python 3
    from builtins import str
import json

from cfgm_common import _obj_serializer_all
from cfgm_common.exceptions import HttpError
from cfgm_common.exceptions import NoIdError
from vnc_api.gen.resource_common import InstanceIp
from vnc_api.gen.resource_common import LogicalInterface
from vnc_api.gen.resource_common import PortTuple
from vnc_api.gen.resource_common import VirtualNetwork
from vnc_api.gen.resource_xsd import IdPermsType

from vnc_cfg_api_server.resources._resource_base import ResourceMixin


class PortTupleServer(ResourceMixin, PortTuple):
    @classmethod
    def get_svc_params_from_pt(cls, obj_dict):
        db_conn = cls.db_conn
        ref_list = None
        si_result = sa_result = None
        si_uuid = obj_dict.get('parent_uuid')
        if si_uuid is None:
            msg = "Port tuple(%s) not associated with service instance" % (
                obj_dict.get('uuid'))
            return False, msg, None, None

        ok, si_result = cls.dbe_read(
            db_conn, 'service_instance',
            si_uuid,
            obj_fields=['service_template_refs',
                        'annotations',
                        'fq_name'])
        if not ok:
            return ok, si_result, None, None

        st = si_result.get('service_template_refs')
        if st and len(st) == 1:
            st_uuid = st[0]['uuid']
            ok, st_result = cls.dbe_read(
                db_conn, 'service_template',
                st_uuid,
                obj_fields=['service_appliance_set_refs'])
            if not ok:
                return ok, st_result, si_result, None

            sas = st_result.get('service_appliance_set_refs')
            if sas and len(sas) == 1:
                sas_uuid = sas[0]['uuid']
                ok, sas_result = cls.dbe_read(
                    db_conn, 'service_appliance_set',
                    sas_uuid,
                    obj_fields=['service_appliances'])
                if not ok:
                    return ok, sas_result, si_result, None

                sa = sas_result.get('service_appliances')
                # Only one SA is supported per SAS for PNF support
                if sa and len(sa) == 1:
                    sa_uuid = sa[0]['uuid']
                    ok, sa_result = cls.dbe_read(
                        db_conn, 'service_appliance',
                        sa_uuid,
                        obj_fields=['physical_interface_refs'])
                    if not ok:
                        return ok, sa_result, si_result, None
                elif sa and len(sa) > 1:
                    msg = (
                        "service appliance set (%s) cannot have more "
                        "than one service appliance" % (sas_uuid))
                else:
                    msg = (
                        "No service appliance for service appliance "
                        "set (%s)" % (sas_uuid))
                    return False, msg, si_result, None
            else:
                if sas:
                    ref_list = [ref.get('to') for ref in sas]
                msg = (
                    "service template (%s) can have only one "
                    "service appliance set, existing refs (%s)" %
                    (st_uuid, ref_list))
                return False, msg, si_result, None
        else:
            if st:
                ref_list = [ref.get('to') for ref in st]
            msg = ("service instance (%s) can have only one service "
                   "template, existing refs (%s)" % (si_uuid,
                                                     ref_list))
            return False, msg, si_result, None

        return True, '', si_result, sa_result

    @staticmethod
    def _construct_iip_fq_name(dev_name, link, vlan_tag):
        return "%s.%s.%s" % (dev_name, link, vlan_tag)

    @staticmethod
    def _construct_li_fq_name(dev_name, link, vlan_tag):
        li_fq_name = ['default-global-system-config', dev_name, link]
        li_fq_name = li_fq_name + ['%s.%s' % (link, vlan_tag)]
        return li_fq_name

    @staticmethod
    def _get_svc_vlans(annotations):
        left_svc_vlan = right_svc_vlan = None
        if annotations is not None:
            kvps = annotations.get('key_value_pair')
            if kvps is not None:
                for d in kvps:
                    if d.get('key') == 'left-svc-vlan':
                        left_svc_vlan = d.get('value')
                    elif d.get('key') == 'right-svc-vlan':
                        right_svc_vlan = d.get('value')

        return left_svc_vlan, right_svc_vlan

    @classmethod
    def _delete_logical_interface(cls, dev_name, link, vlan_tag):
        iip_uuid = None
        li_uuid = None
        api_server = cls.server
        db_conn = cls.db_conn

        iip_fqname = [cls._construct_iip_fq_name(dev_name, link, vlan_tag)]
        try:
            iip_uuid = db_conn.fq_name_to_uuid('instance_ip', iip_fqname)
        except NoIdError:
            pass
        if iip_uuid is not None:
            try:
                api_server.internal_request_delete('instance_ip', iip_uuid)
            except HttpError as e:
                return False, (e.status_code, e.content)

        li_fqname = cls._construct_li_fq_name(dev_name, link, vlan_tag)
        try:
            li_uuid = db_conn.fq_name_to_uuid('logical_interface', li_fqname)
        except NoIdError:
            pass
        if li_uuid is not None:
            try:
                api_server.internal_request_delete(
                    'logical_interface', li_uuid)
            except HttpError as e:
                return False, (e.status_code, e.content)

        return True, ''

    @classmethod
    def _create_logical_interface(cls, dev_name, link,
                                  vlan_tag, subscriber_tag,
                                  network_name):
        api_server = cls.server
        db_conn = cls.db_conn

        li_fqname = cls._construct_li_fq_name(dev_name, link, vlan_tag)
        li_display_name = li_fqname[-1]
        li_display_name = li_display_name.replace("_", ":")
        id_perms = IdPermsType(enable=True, user_visible=True)
        li_obj = LogicalInterface(parent_type='physical-interface',
                                  fq_name=li_fqname,
                                  logical_interface_vlan_tag=vlan_tag,
                                  display_name=li_display_name,
                                  id_perms=id_perms)
        li_int_dict = json.dumps(li_obj, default=_obj_serializer_all)
        try:
            li_uuid = db_conn.fq_name_to_uuid('logical_interface', li_fqname)
            try:
                api_server.internal_request_update(
                    'logical-interface',
                    li_uuid,
                    json.loads(li_int_dict))
            except HttpError as e:
                return False, (e.status_code, e.content)
        except NoIdError:
            try:
                api_server.internal_request_create(
                    'logical-interface', json.loads(li_int_dict))
            except HttpError as e:
                return False, (e.status_code, e.content)

        # Allocate IP address for this logical interface
        iip_name = cls._construct_iip_fq_name(dev_name, link, vlan_tag)
        if subscriber_tag is not None:
            iip_obj = InstanceIp(
                name=iip_name,
                instance_ip_family='v4',
                instance_ip_subscriber_tag=subscriber_tag
            )
        else:
            iip_obj = InstanceIp(
                name=iip_name,
                instance_ip_family='v4'
            )
        nw_fq_name = ['default-domain', 'default-project', network_name]
        ok, nw_obj_result = cls.server.get_resource_class(
            'virtual_network').locate(nw_fq_name, create_it=False)
        if not ok:
            return False, nw_obj_result

        nw_obj = VirtualNetwork(name=network_name)
        iip_obj.set_virtual_network(nw_obj)
        iip_obj.set_logical_interface(li_obj)
        iip_obj.set_id_perms(id_perms)
        iip_int_dict = json.dumps(iip_obj, default=_obj_serializer_all)
        try:
            iip_uuid = db_conn.fq_name_to_uuid('instance_ip', [iip_name])
            try:
                api_server.internal_request_update(
                    'instance-ip',
                    iip_uuid,
                    json.loads(iip_int_dict))
            except HttpError as e:
                return False, (e.status_code, e.content)
        except NoIdError:
            try:
                api_server.internal_request_create(
                    'instance-ip', json.loads(iip_int_dict))
            except HttpError as e:
                return False, (e.status_code, e.content)

        return True, ''

    @classmethod
    def post_dbe_create(cls, tenant_name, obj_dict, db_conn):
        # Create logical interfaces for the physical interfaces using
        # vlan allocated per service instance

        # IP addresese are allocated from the fabric-service-chain subnet for
        # these logical interfaces using IPAM
        ok, msg, si, sa = cls.get_svc_params_from_pt(obj_dict)
        if not ok:
            return (False, (400, msg))

        svc_inst_name = si.get('fq_name')[-1]

        # Fetch left and right vlan from the service instance
        left_svc_vlan, right_svc_vlan = cls._get_svc_vlans(
            si.get('annotations'))
        if left_svc_vlan and right_svc_vlan:
            for phys_intf_ref in sa.get(
                    'physical_interface_refs') or []:
                ok, phys_intf_result = cls.dbe_read(
                    db_conn, 'physical_interface',
                    phys_intf_ref['uuid'], obj_fields=[
                        'fq_name', 'parent_type', 'parent_uuid',
                        'physical_interface_refs'])
                if not ok:
                    return ok, phys_intf_result

                phys_intf_name = phys_intf_result.get('fq_name')[-1]
                dev_name = phys_intf_result.get('fq_name')[-2]

                if phys_intf_result.get(
                        'parent_type') == 'physical-router':
                    phys_router_uuid = phys_intf_result.get(
                        'parent_uuid')
                    ok, phys_router_result = cls.dbe_read(
                        db_conn, 'physical_router',
                        phys_router_uuid, obj_fields=['fabric_refs'])
                    if not ok:
                        return ok, phys_router_result
                    fabric_refs = phys_router_result.get('fabric_refs')
                    if fabric_refs and len(fabric_refs) != 1:
                        ref_list = [ref.get('to') for ref in fabric_refs]
                        msg = "No or more than one fabric for physical"
                        " router(%s), existing refs (%s)" % (phys_router_uuid,
                                                             ref_list)
                        return (False, (400, msg))
                    fabric_name = str(fabric_refs[0].get('to')[-1])
                    svc_network_type = 'pnf-servicechain'
                    svc_network_name = '%s-%s-network' % (
                        fabric_name, svc_network_type)

                    if phys_intf_ref['attr'].get(
                            'interface_type') == 'left':
                        vlan_tag = left_svc_vlan
                        svc_subscriber_tag = svc_inst_name + '-' + 'left'
                    elif phys_intf_ref['attr'].get(
                            'interface_type') == 'right':
                        vlan_tag = right_svc_vlan
                        svc_subscriber_tag = svc_inst_name + '-' + 'right'

                    # Create logical interfaces for the PNF device
                    ok, result = cls._create_logical_interface(
                        dev_name,
                        phys_intf_name,
                        vlan_tag,
                        svc_subscriber_tag,
                        svc_network_name)
                    if not ok:
                        return False, result

                    loopback_network_type = 'loopback'
                    loopback_network_name = '%s-%s-network' % (
                        fabric_name, loopback_network_type)
                    # Create one dummy loopback interface for the PNF
                    # device
                    ok, result = cls._create_logical_interface(
                        dev_name, 'lo0', left_svc_vlan,
                        None, loopback_network_name)
                    if not ok:
                        return False, result

                    for ref in phys_intf_result.get(
                            'physical_interface_refs') or []:
                        ok, read_result = cls.dbe_read(
                            db_conn, 'physical_interface',
                            ref['uuid'], obj_fields=['fq_name'])
                        if not ok:
                            return ok, read_result
                        phys_intf_name = read_result.get('fq_name')[-1]
                        dev_name = read_result.get('fq_name')[-2]
                        # Create logical interfaces for spine
                        ok, result = cls._create_logical_interface(
                            dev_name,
                            phys_intf_name,
                            vlan_tag,
                            svc_subscriber_tag,
                            svc_network_name)
                        if not ok:
                            return False, result

        return True, ''
    # end post_dbe_create

    @classmethod
    def pre_dbe_delete(cls, id, obj_dict, db_conn):
        # Delete the logical interfaces created and de-allocate IP addresses
        # from them
        ok, msg, si, sa = cls.get_svc_params_from_pt(obj_dict)
        if si and sa:
            # Fetch left and right vlan from the service instance
            left_svc_vlan, right_svc_vlan = cls._get_svc_vlans(
                si.get('annotations'))
            if left_svc_vlan and right_svc_vlan:
                for phys_intf_ref in sa.get(
                        'physical_interface_refs') or []:
                    ok, phys_intf_result = cls.dbe_read(
                        db_conn, 'physical_interface',
                        phys_intf_ref['uuid'], obj_fields=[
                            'fq_name', 'physical_interface_refs'])
                    if not ok:
                        return ok, phys_intf_result, None

                    phys_intf_name = phys_intf_result.get('fq_name')[-1]
                    dev_name = phys_intf_result.get('fq_name')[-2]
                    if phys_intf_ref['attr'].get('interface_type') == 'left':
                        vlan_tag = left_svc_vlan
                    elif phys_intf_ref['attr'].get(
                            'interface_type') == 'right':
                        vlan_tag = right_svc_vlan

                    # Delete logical interfaces for the PNF device
                    ok, result = cls._delete_logical_interface(
                        dev_name, phys_intf_name, vlan_tag)
                    if not ok:
                        return False, (400, result), None

                    # Delete dummy loopback interface for the PNF device
                    ok, result = cls._delete_logical_interface(dev_name, 'lo0',
                                                               left_svc_vlan)
                    if not ok:
                        return False, (400, result), None

                    for ref in phys_intf_result.get(
                            'physical_interface_refs') or []:
                        ok, read_result = cls.dbe_read(
                            db_conn, 'physical_interface',
                            ref['uuid'], obj_fields=['fq_name'])
                        if not ok:
                            return ok, read_result, None
                        phys_intf_name = read_result.get('fq_name')[-1]
                        dev_name = read_result.get('fq_name')[-2]
                        # Delete logical interfaces for spine
                        ok, result = cls._delete_logical_interface(
                            dev_name, phys_intf_name, vlan_tag)
                        if not ok:
                            return False, (400, result), None

        return True, '', None
    # end pre_dbe_delete
# end class PortTupleServer
