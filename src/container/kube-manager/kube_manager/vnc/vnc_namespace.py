#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

"""
VNC service management for kubernetes
"""

from vnc_api.vnc_api import *
from config_db import *
from kube_manager.common.kube_config_db import NamespaceKM

class VncNamespace(object):

    def __init__(self, cluster_pod_subnets=None, vnc_lib=None):
        self._vnc_lib = vnc_lib

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

    def vnc_namespace_add(self, namespace_id, name):
        proj_fq_name = ['default-domain', name]
        proj_obj = Project(name=name, fq_name=proj_fq_name)
        try:
            self._vnc_lib.project_create(proj_obj)
        except RefsExistError:
            proj_obj = self._vnc_lib.project_read(fq_name=proj_fq_name)

        ProjectKM.locate(proj_obj.uuid)

        # If this namespace is isolated, create it own network.
        if self._is_namespace_isolated(name) == True:
            vn_name = name + "-vn"
            self._create_virtual_network(ns_name= name, vn_name=vn_name,
                                         proj_obj = proj_obj)


        return proj_obj

    def vnc_namespace_delete(self,namespace_id,  name):
        proj_fq_name = ['default-domain', name]
        proj_obj = Project(name=name, fq_name=proj_fq_name)
        try:
            # If the namespace is isolated, delete its virtual network.
            if self._is_namespace_isolated(name) == True:
                self._delete_virtual_network(vn_name=name, proj=proj_obj)

            self._vnc_lib.project_delete(fq_name=['default-domain', name])

            self._delete_namespace(name)

        except NoIdError:
            pass

    def process(self, event):
        name = event['object']['metadata'].get('name')
        ns_id = event['object']['metadata'].get('uid')

        if event['type'] == 'ADDED':
            self.vnc_namespace_add(ns_id, name)
        elif event['type'] == 'DELETED':
            self.vnc_namespace_delete(ns_id, name)
