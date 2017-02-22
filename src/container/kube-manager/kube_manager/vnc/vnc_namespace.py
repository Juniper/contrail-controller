#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#

"""
VNC service management for kubernetes
"""

import uuid

from vnc_api.vnc_api import *
from config_db import *
from kube_manager.common.kube_config_db import NamespaceKM

class VncNamespace(object):

    def __init__(self, cluster_pod_subnets=None, logger=None, vnc_lib=None):
        self._name = type(self).__name__
        self._vnc_lib = vnc_lib
        self._logger = logger

        # Cache user specified subnet directive to be used for pods iip's
        # in this namespace.
        self._cluster_pod_subnets = cluster_pod_subnets

    def _get_namespace(self, ns_name):
        """
        Get namesapce object from cache.
        """
        return NamespaceKM.find_by_name_or_uuid(ns_name)

    def _delete_namespace(self, ns_name):
        """
        Delete namespace object from cache.
        """
        ns = self._get_namespace(ns_name)
        if ns:
            NamespaceKM.delete(ns.uuid)

    def _is_namespace_isolated(self, ns_name):
        """
        Check if this namespace is configured as isolated.
        """
        ns = self._get_namespace(ns_name)
        if ns:
            return ns.is_isolated()

        # By default, namespace is not isolated.
        return False

    def _set_namespace_virtual_network(self, ns_name, fq_name):
        ns = self._get_namespace(ns_name)
        if ns:
            return ns.set_network_fq_name(fq_name)
        return None

    def _create_ipam(self, ipam_name, subnets, proj_obj, type):
        """
        Create an ipam.
        If an ipam with same name exists, return the existing object.
        """
        ipam_subnets = []
        for subnet in subnets:
            pfx, pfx_len = subnet.split('/')
            ipam_subnet = IpamSubnetType(subnet=SubnetType(pfx, int(pfx_len)))
            ipam_subnets.append(ipam_subnet)

        ipam_obj = NetworkIpam(name=ipam_name, parent_obj=proj_obj)
        if type == 'flat-subnet':
            ipam_obj.set_ipam_subnet_method('flat-subnet')
            ipam_obj.set_ipam_subnets(IpamSubnets(ipam_subnets))

        try:
            self._vnc_lib.network_ipam_create(ipam_obj)
        except RefsExistError:
            pass

        ipam = self._vnc_lib.network_ipam_read(fq_name=ipam_obj.get_fq_name())

        # Cache ipam info.
        NetworkIpamKM.locate(ipam.uuid)

        return ipam_obj, ipam_subnets

    def _delete_ipam(self, ipam):
        """
        Delete the requested ipam.
        """
        self._vnc_lib.network_ipam_delete(id=ipam['uuid'])

        # Remove the ipam from cache.
        NetworkIpamSM.delete(ipam['uuid'])

    def _create_virtual_network(self, ns_name, vn_name, proj_obj, subnets=None):
        """
        Create a virtual network for this namespace.
        """
        vn = VirtualNetwork(name=vn_name, parent_obj=proj_obj,
            virtual_network_properties=VirtualNetworkType(forwarding_mode='l3'),
            address_allocation_mode='flat-subnet-only')
        try:
            vn_obj = self._vnc_lib.virtual_network_read(
                fq_name=vn.get_fq_name())

        except NoIdError:
            # Virtual network does not exist. Create one.
            self._vnc_lib.virtual_network_create(vn)

        if not subnets:
            # If subnet info is not specified in namespace configuration,
            # this namespace will use kubernetes cluster pod network subnet.
            subnets = self._cluster_pod_subnets

        # Create the ipam object.
        ipam_obj, ipam_subnets= self._create_ipam('pod-ipam-%s' % (vn_name),
            subnets=subnets, proj_obj=proj_obj, type='flat-subnet')

        # Attach the ipam object to the virtual network.
        vn.add_network_ipam(ipam_obj, VnSubnetsType([]))
        self._vnc_lib.virtual_network_update(vn)

        # Cache the virtual network.
        VirtualNetworkKM.locate(vn.uuid)

        # Cache network info in namespace entry.
        self._set_namespace_virtual_network(ns_name, vn.fq_name)

        return

    def _delete_virtual_network(self, ns_name, vn_name, proj):
        """
        Delete the virtual network associated with this namespace.
        """

        # First lookup the cache for the entry.
        vn = VirtualNetworkKM.find_by_name_or_uuid(vn_name)
        if not vn:
            return

        try:
            # Delete/cleanup ipams allocated for this network.
            vn_obj = self._vnc_lib.virtual_network_read(fq_name=vn.fq_name)
            if vn_obj.get_network_ipam_refs():
                ipam_refs = vn_obj.get_network_ipam_refs()
                for ipam in ipam_refs:
                    ipam_obj = NetworkIpam(name=ipam['to'][-1],parent_obj=proj)
                    vn_obj.del_network_ipam(ipam_obj)
                    self._vnc_lib.virtual_network_update(vn_obj)
                    self._delete_ipam(ipam)
        except NoIdError:
            pass

        # Delete the network.
        self._vnc_lib.virtual_network_delete(id=vn.uuid)

        # Delete the network from cache.
        VirtualNetworkKM.delete(vn.uuid)

        # Clear network info from namespace entry.
        self._set_namespace_virtual_network(ns_name, None)

    def _create_default_security_group(self, proj_obj, network_policy):
        DEFAULT_SECGROUP_DESCRIPTION = "Default security group"
        def _get_rule(ingress, sg, prefix, ethertype):
            sgr_uuid = str(uuid.uuid4())
            if sg:
                addr = AddressType(
                    security_group=proj_obj.get_fq_name_str() + ':' + sg)
            elif prefix:
                addr = AddressType(subnet=SubnetType(prefix, 0))
            local_addr = AddressType(security_group='local')
            if ingress:
                src_addr = addr
                dst_addr = local_addr
            else:
                src_addr = local_addr
                dst_addr = addr
            rule = PolicyRuleType(rule_uuid=sgr_uuid, direction='>',
                                  protocol='any',
                                  src_addresses=[src_addr],
                                  src_ports=[PortType(0, 65535)],
                                  dst_addresses=[dst_addr],
                                  dst_ports=[PortType(0, 65535)],
                                  ethertype=ethertype)
            return rule

        rules = []
        ingress = True
        egress = True
        if network_policy and 'ingress' in network_policy:
            ingress_policy = network_policy['ingress']
            if ingress_policy and 'isolation' in ingress_policy:
                isolation = ingress_policy['isolation']
                if isolation == 'DefaultDeny':
                    ingress = False
        if ingress:
            rules.append(_get_rule(True, 'default', None, 'IPv4'))
            rules.append(_get_rule(True, 'default', None, 'IPv6'))
        if egress:
            rules.append(_get_rule(False, None, '0.0.0.0', 'IPv4'))
            rules.append(_get_rule(False, None, '::', 'IPv6'))
        sg_rules = PolicyEntriesType(rules)

        # create security group
        id_perms = IdPermsType(enable=True,
                               description=DEFAULT_SECGROUP_DESCRIPTION)
        sg_obj = SecurityGroup(name='default', parent_obj=proj_obj,
                               id_perms=id_perms,
                               security_group_entries=sg_rules)

        self._vnc_lib.security_group_create(sg_obj)
        self._vnc_lib.chown(sg_obj.get_uuid(), proj_obj.get_uuid())

    def vnc_namespace_add(self, namespace_id, name, annotations):
        proj_fq_name = ['default-domain', name]
        proj_obj = Project(name=name, fq_name=proj_fq_name)

        try:
            self._vnc_lib.project_create(proj_obj)
        except RefsExistError:
            proj_obj = self._vnc_lib.project_read(fq_name=proj_fq_name)

        try:
            security_groups = proj_obj.get_security_groups()
            for sg in security_groups or []:
                self._vnc_lib.security_group_delete(id=sg['uuid'])
            network_policy = None
            if annotations and \
               'net.beta.kubernetes.io/network-policy' in annotations:
                network_policy = json.loads(annotations['net.beta.kubernetes.io/network-policy'])
            self._create_default_security_group(proj_obj, network_policy)
        except RefsExistError:
            pass

        ProjectKM.locate(proj_obj.uuid)

        # If this namespace is isolated, create it own network.
        if self._is_namespace_isolated(name) == True:
            vn_name = name + "-vn"
            self._create_virtual_network(ns_name= name, vn_name=vn_name,
                                         proj_obj = proj_obj)

        return proj_obj

    def vnc_namespace_delete(self,namespace_id,  name):
        proj_fq_name = ['default-domain', name]
        proj_obj = self._vnc_lib.project_read(fq_name=proj_fq_name)
        try:
            # If the namespace is isolated, delete its virtual network.
            if self._is_namespace_isolated(name) == True:
                self._delete_virtual_network(vn_name=name, proj=proj_obj)
            # delete all security groups
            security_groups = proj_obj.get_security_groups()
            for sg in security_groups or []:
                self._vnc_lib.security_group_delete(id=sg['uuid'])
            self._vnc_lib.project_delete(id=proj_obj.uuid)
            self._delete_namespace(name)
        except NoIdError:
            pass

    def vnc_namespace_delete(self,namespace_id,  name):
        proj_fq_name = ['default-domain', name]
        proj_obj = self._vnc_lib.project_read(fq_name=proj_fq_name)
        try:
            # If the namespace is isolated, delete its virtual network.
            if self._is_namespace_isolated(name) == True:
                self._delete_virtual_network(vn_name=name, proj=proj_obj)
            # delete all security groups
            security_groups = proj_obj.get_security_groups()
            for sg in security_groups or []:
                self._vnc_lib.security_group_delete(id=sg['uuid'])
            # delete the project
            self._vnc_lib.project_delete(fq_name=['default-domain', name])
            # delete the namespace
            self._delete_namespace(name)
        except NoIdError:
            pass

    def process(self, event):
        event_type = event['type']
        kind = event['object'].get('kind')
        name = event['object']['metadata'].get('name')
        ns_id = event['object']['metadata'].get('uid')
        annotations = event['object']['metadata'].get('annotations')
        print("%s - Got %s %s %s"
              %(self._name, event_type, kind, name))
        self._logger.debug("%s - Got %s %s %s"
              %(self._name, event_type, kind, name))


        if event['type'] == 'ADDED' or event['type'] == 'MODIFIED':
            self.vnc_namespace_add(ns_id, name, annotations)
        elif event['type'] == 'DELETED':
            self.vnc_namespace_delete(ns_id, name)
