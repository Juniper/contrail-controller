#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

"""
VNC network management for kubernetes.
"""
from __future__ import print_function

from builtins import str
from netaddr import IPNetwork, IPAddress
from six import string_types
import uuid

from cfgm_common.exceptions import RefsExistError, NoIdError
from vnc_api.gen.resource_client import (
    NetworkIpam, VirtualNetwork
)
from vnc_api.gen.resource_xsd import (
    IpamSubnets, IpamSubnetType, SubnetType,
    VirtualNetworkType, VnSubnetsType
)

from kube_manager.vnc.config_db import (
    NetworkIpamKM, VirtualNetworkKM
)
from kube_manager.vnc.vnc_kubernetes_config import VncKubernetesConfig as vnc_kube_config
from kube_manager.vnc.vnc_common import VncCommon
from kube_manager.common.kube_config_db import NetworkKM


class VncNetwork(VncCommon):

    def __init__(self):
        self._k8s_event_type = 'Network'
        super(VncNetwork, self).__init__(self._k8s_event_type)
        self._name = type(self).__name__
        self._vnc_lib = vnc_kube_config.vnc_lib()
        self._args = vnc_kube_config.args()
        self._logger = vnc_kube_config.logger()
        self._queue = vnc_kube_config.queue()

    def process(self, event):
        event_type = event['type']
        kind = event['object'].get('kind')
        name = event['object']['metadata'].get('name')
        ns_id = event['object']['metadata'].get('uid')
        namespace = event['object']['metadata'].get('namespace')
        annotations = event['object']['metadata'].get('annotations')
        print(
            "%s - Got %s %s %s:%s"
            % (self._name, event_type, kind, name, ns_id))
        self._logger.debug(
            "%s - Got %s %s %s:%s Namespace: %s"
            % (self._name, event_type, kind, name, ns_id, namespace))

        if event['type'] == 'ADDED' or event['type'] == 'MODIFIED':
            # If CIDR is provided in the network YAML, we need to Create
            # the virtual network and IPAM objects in contrail, else need to do
            # nothing here as network already exists
            if 'opencontrail.org/cidr' in annotations:
                subnets = annotations.get('opencontrail.org/cidr', None)
                ann_val = annotations.get(
                    'opencontrail.org/ip_fabric_forwarding', 'false')
                if ann_val.lower() == 'true':
                    ip_fabric_forwarding = True
                elif ann_val.lower() == 'false':
                    ip_fabric_forwarding = False
                else:
                    ip_fabric_forwarding = None
                ann_val = annotations.get(
                    'opencontrail.org/ip_fabric_snat', 'false')
                if ann_val.lower() == 'true':
                    ip_fabric_snat = True
                elif ann_val.lower() == 'false':
                    ip_fabric_snat = False
                else:
                    ip_fabric_snat = None

                # Get Newtork object associated with the namespace
                # and network name
                nw = NetworkKM.get_network_fq_name(name, namespace)
                if not nw:
                    self._logger.error(
                        "%s -Error in retrieving Network"
                        " object for VN_Name %s and Namespace %s "
                        % (self._name, name, namespace))
                    return
                nw.annotated_vn_fq_name = self._make_vn_fq_name(
                    ns_name=namespace, vn_name=name)

                # Get project and IPAM for given subnet if it does not exist
                proj_obj = self._get_project(namespace)
                if proj_obj is None:
                    self._logger.error(
                        "%s -Error in retrieving Project"
                        " for namespace %s " % (self._name, namespace))
                    return

                self._logger.debug("%s - Create IPAM- Subnets %s"
                                   % (self._name, subnets))
                subnet_data = self._create_subnet_data(subnets)
                ipam_name = self._get_network_pod_ipam_name(name)

                vn_name = self._get_network_pod_vn_name(name)
                pod_ipam_update, pod_ipam_obj, pod_ipam_subnets = \
                    self._create_ipam(ipam_name=ipam_name, proj_obj=proj_obj)

                provider = None
                if ip_fabric_forwarding:
                    ip_fabric_fq_name = vnc_kube_config.\
                        cluster_ip_fabric_network_fq_name()
                    provider = self._vnc_lib.\
                        virtual_network_read(fq_name=ip_fabric_fq_name)

                # Create virtual network if it does not exist
                self._create_virtual_network(
                    vn_name=vn_name, proj_obj=proj_obj, provider=provider,
                    ipam_obj=pod_ipam_obj, ipam_update=pod_ipam_update,
                    subnets=subnet_data, type='user-defined-subnet-only',
                    ip_fabric_snat=ip_fabric_snat)

        elif event['type'] == 'DELETED':
            vn_name = self._get_network_pod_vn_name(name)
            self._delete_virtual_network(ns_name=namespace, vn_name=vn_name)
        else:
            self._logger.warning(
                'Unknown event type: "{}" in Network creation. \
                Ignoring'.format(event['type']))

    def _get_network_pod_ipam_name(self, nw_name):
        return vnc_kube_config.cluster_name() + '-' + nw_name + '-pod-ipam'

    def _get_network_pod_vn_name(self, nw_name):
        return vnc_kube_config.cluster_name() + '-' + nw_name + "-pod-network"

    def _make_vn_fq_name(self, ns_name, vn_name, domain_name='default-domain'):
        vn_fq_name = []
        vn_fq_name.append(domain_name)
        project_name = vnc_kube_config.cluster_project_name(ns_name)
        vn_fq_name.append(project_name)
        virtual_net_name = vnc_kube_config.get_pod_network_name(vn_name)
        vn_fq_name.append(virtual_net_name)
        return vn_fq_name

    def _create_subnet_data(self, vn_subnet):
        subnets = [vn_subnet] if isinstance(vn_subnet, string_types) else vn_subnet
        subnet_infos = []
        for subnet in subnets:
            cidr = IPNetwork(subnet)
            subnet_infos.append(
                IpamSubnetType(
                    subnet=SubnetType(
                        str(cidr.network),
                        int(cidr.prefixlen),
                    ),
                    default_gateway=str(IPAddress(cidr.last - 1)),
                    subnet_uuid=str(uuid.uuid4()),
                )
            )
        subnet_data = VnSubnetsType(subnet_infos)
        return subnet_data

    def _get_project(self, service_namespace):
        proj_fq_name =\
            vnc_kube_config.cluster_project_fq_name(service_namespace)
        try:
            proj_obj = self._vnc_lib.project_read(fq_name=proj_fq_name)
            return proj_obj
        except NoIdError:
            return None

    def _create_ipam(self, ipam_name, proj_obj, subnets=[], type=None):
        ipam_obj = NetworkIpam(name=ipam_name, parent_obj=proj_obj)

        ipam_subnets = []
        for subnet in subnets:
            pfx, pfx_len = subnet.split('/')
            ipam_subnet = IpamSubnetType(subnet=SubnetType(pfx, int(pfx_len)))
            ipam_subnets.append(ipam_subnet)
        if not len(ipam_subnets):
            self._logger.debug(
                "%s - %s subnet is empty for %s"
                % (self._name, ipam_name, subnets))

        if type == 'flat-subnet':
            ipam_obj.set_ipam_subnet_method('flat-subnet')
            ipam_obj.set_ipam_subnets(IpamSubnets(ipam_subnets))

        ipam_update = False
        try:
            ipam_uuid = self._vnc_lib.network_ipam_create(ipam_obj)
            ipam_update = True
        except RefsExistError:
            curr_ipam_obj = self._vnc_lib.network_ipam_read(
                fq_name=ipam_obj.get_fq_name())
            ipam_uuid = curr_ipam_obj.get_uuid()
            if type == 'flat-subnet' and not curr_ipam_obj.get_ipam_subnets():
                self._vnc_lib.network_ipam_update(ipam_obj)
                ipam_update = True

        # Cache ipam info.
        NetworkIpamKM.locate(ipam_uuid)

        return ipam_update, ipam_obj, ipam_subnets

    def _is_ipam_exists(self, vn_obj, ipam_fq_name, subnet=None):
        curr_ipam_refs = vn_obj.get_network_ipam_refs()
        if curr_ipam_refs:
            for ipam_ref in curr_ipam_refs:
                if ipam_fq_name == ipam_ref['to']:
                    if subnet:
                        # Subnet is specified.
                        # Validate that we are able to match subnect as well.
                        if len(ipam_ref['attr'].ipam_subnets) and \
                                subnet == ipam_ref['attr'].ipam_subnets[0].subnet:
                            return True
                    else:
                        # Subnet is not specified.
                        # So ipam-fq-name match will suffice.
                        return True
        return False

    def _create_virtual_network(self, vn_name, proj_obj, ipam_obj,
                                ipam_update, provider=None, subnets=None,
                                type='flat-subnet-only', ip_fabric_snat=None):
        vn_exists = False
        vn = VirtualNetwork(
            name=vn_name, parent_obj=proj_obj,
            address_allocation_mode=type)
        try:
            vn_obj = self._vnc_lib.virtual_network_read(
                fq_name=vn.get_fq_name())
            vn_exists = True
        except NoIdError:
            # VN does not exist. Create one.
            vn_obj = vn

        if vn_exists:
            return vn_obj

        # Attach IPAM to virtual network.
        #
        # For flat-subnets, the subnets are specified on the IPAM and
        # not on the virtual-network to IPAM link. So pass an empty
        # list of VnSubnetsType.
        # For user-defined-subnets, use the provided subnets
        if ipam_update or \
           not self._is_ipam_exists(vn_obj, ipam_obj.get_fq_name()):
            if subnets and type == 'user-defined-subnet-only':
                vn_obj.add_network_ipam(ipam_obj, subnets)
            else:
                vn_obj.add_network_ipam(ipam_obj, VnSubnetsType([]))

        vn_obj.set_virtual_network_properties(
            VirtualNetworkType(forwarding_mode='l2_l3'))

        if not vn_exists:
            if provider:
                # enable ip_fabric_forwarding
                vn_obj.add_virtual_network(provider)
            elif ip_fabric_snat:
                # enable fabric_snat
                vn_obj.set_fabric_snat(True)
            else:
                # disable fabric_snat
                vn_obj.set_fabric_snat(False)
            # Create VN.
            self._vnc_lib.virtual_network_create(vn_obj)
        else:
            # TODO: Handle Network update
            pass

        vn_obj = self._vnc_lib.virtual_network_read(
            fq_name=vn_obj.get_fq_name())
        VirtualNetworkKM.locate(vn_obj.uuid)

        return vn_obj

    def _delete_virtual_network(self, ns_name, vn_name):
        """
        Delete the virtual network associated with this namespace.
        """
        # First lookup the cache for the entry.
        vn = VirtualNetworkKM.find_by_name_or_uuid(vn_name)
        if not vn:
            return

        proj_fq_name = vnc_kube_config.cluster_project_fq_name(ns_name)
        try:
            vn_obj = self._vnc_lib.virtual_network_read(fq_name=vn.fq_name)
            # Delete/cleanup ipams allocated for this network.
            ipam_refs = vn_obj.get_network_ipam_refs()
            if ipam_refs:
                proj_obj = self._vnc_lib.project_read(fq_name=proj_fq_name)
                for ipam in ipam_refs:
                    ipam_obj = NetworkIpam(
                        name=ipam['to'][-1], parent_obj=proj_obj)
                    vn_obj.del_network_ipam(ipam_obj)
                    self._vnc_lib.virtual_network_update(vn_obj)
        except RefsExistError as e:
            # Delete of custom network when it is still in use is not
            # supported yet. Log deletion attempt and return without deleting VN
            self._logger.error("%s: Cannot delete Network %s . %s"
                               % (self._name, vn_name, str(e)))
            return
        except NoIdError:
            pass

        # Delete the network.
        self._vnc_lib.virtual_network_delete(id=vn.uuid)

        # Delete the network from cache.
        VirtualNetworkKM.delete(vn.uuid)
