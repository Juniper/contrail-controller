#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#

"""
VNC service management for kubernetes.
"""
from __future__ import print_function

from builtins import str
import uuid

import cfgm_common
from cfgm_common.exceptions import RefsExistError, NoIdError
from vnc_api.gen.resource_client import (
    NetworkIpam, Project, SecurityGroup, VirtualNetwork, NetworkPolicy
)
from vnc_api.gen.resource_xsd import (
    AddressType, IdPermsType, SequenceType, ShareType,
    PolicyEntriesType, PolicyRuleType, PortType, SubnetType,
    VirtualNetworkType, VnSubnetsType, ActionListType, VirtualNetworkPolicyType
)

from kube_manager.vnc.config_db import (
    VirtualNetworkKM, ProjectKM, SecurityGroupKM, DBBaseKM)
from kube_manager.common.kube_config_db import NamespaceKM, PodKM
from kube_manager.vnc.vnc_kubernetes_config import VncKubernetesConfig as vnc_kube_config
from kube_manager.vnc.vnc_common import VncCommon
from kube_manager.vnc.label_cache import XLabelCache
from kube_manager.vnc.vnc_pod import VncPod
from kube_manager.vnc.vnc_security_policy import VncSecurityPolicy


class VncNamespace(VncCommon):

    def __init__(self, network_policy_mgr):
        self._k8s_event_type = 'Namespace'
        super(VncNamespace, self).__init__(self._k8s_event_type)
        self._name = type(self).__name__
        self._network_policy_mgr = network_policy_mgr
        self._vnc_lib = vnc_kube_config.vnc_lib()
        self._label_cache = vnc_kube_config.label_cache()
        self._args = vnc_kube_config.args()
        self._logger = vnc_kube_config.logger()
        self._queue = vnc_kube_config.queue()
        self._labels = XLabelCache(self._k8s_event_type)
        ip_fabric_fq_name = vnc_kube_config. \
            cluster_ip_fabric_network_fq_name()
        self._ip_fabric_vn_obj = self._vnc_lib. \
            virtual_network_read(fq_name=ip_fabric_fq_name)
        self._ip_fabric_policy = None
        self._cluster_service_policy = None
        self._nested_underlay_policy = None

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

    def _get_namespace_pod_vn_name(self, ns_name):
        return vnc_kube_config.cluster_name() + \
            '-' + ns_name + "-pod-network"

    def _get_namespace_service_vn_name(self, ns_name):
        return vnc_kube_config.cluster_name() + \
            '-' + ns_name + "-service-network"

    def _get_ip_fabric_forwarding(self, ns_name):
        ns = self._get_namespace(ns_name)
        if ns:
            return ns.get_ip_fabric_forwarding()
        return None

    def _is_ip_fabric_forwarding_enabled(self, ns_name):
        ip_fabric_forwarding = self._get_ip_fabric_forwarding(ns_name)
        if ip_fabric_forwarding is not None:
            return ip_fabric_forwarding
        else:
            return self._args.ip_fabric_forwarding

    def _get_ip_fabric_snat(self, ns_name):
        ns = self._get_namespace(ns_name)
        if ns:
            return ns.get_ip_fabric_snat()
        return None

    def _is_ip_fabric_snat_enabled(self, ns_name):
        ip_fabric_snat = self._get_ip_fabric_snat(ns_name)
        if ip_fabric_snat is not None:
            return ip_fabric_snat
        else:
            return self._args.ip_fabric_snat

    def _is_namespace_isolated(self, ns_name):
        """
        Check if this namespace is configured as isolated.
        """
        ns = self._get_namespace(ns_name)
        if ns:
            return ns.is_isolated()

        # Kubernetes namespace obj is not available to check isolation config.
        #
        # Check if the virtual network associated with the namespace is
        # annotated as isolated. If yes, then the namespace is isolated.
        vn_uuid = VirtualNetworkKM.get_ann_fq_name_to_uuid(self, ns_name,
                                                           ns_name)
        if vn_uuid:
            vn_obj = VirtualNetworkKM.get(vn_uuid)
            if vn_obj:
                return vn_obj.is_k8s_namespace_isolated()

        # By default, namespace is not isolated.
        return False

    def _get_network_policy_annotations(self, ns_name):
        ns = self._get_namespace(ns_name)
        if ns:
            return ns.get_network_policy_annotations()
        return None

    def _get_annotated_virtual_network(self, ns_name):
        ns = self._get_namespace(ns_name)
        if ns:
            return ns.get_annotated_network_fq_name()
        return None

    def _get_annotated_ns_fip_pool(self, ns_name):
        ns = self._get_namespace(ns_name)
        if ns:
            return ns.get_annotated_ns_fip_pool_fq_name()
        return None

    def _set_namespace_pod_virtual_network(self, ns_name, fq_name):
        ns = self._get_namespace(ns_name)
        if ns:
            return ns.set_isolated_pod_network_fq_name(fq_name)
        return None

    def _set_namespace_service_virtual_network(self, ns_name, fq_name):
        ns = self._get_namespace(ns_name)
        if ns:
            return ns.set_isolated_service_network_fq_name(fq_name)
        return None

    def _clear_namespace_label_cache(self, ns_uuid, project):
        if not ns_uuid or \
           ns_uuid not in project.ns_labels:
            return
        ns_labels = project.ns_labels[ns_uuid]
        for label in list(ns_labels.items()) or []:
            key = self._label_cache._get_key(label)
            self._label_cache._remove_label(
                key, self._label_cache.ns_label_cache, label, ns_uuid)
        del project.ns_labels[ns_uuid]

    def _update_namespace_label_cache(self, labels, ns_uuid, project):
        self._clear_namespace_label_cache(ns_uuid, project)
        for label in list(labels.items()):
            key = self._label_cache._get_key(label)
            self._label_cache._locate_label(
                key, self._label_cache.ns_label_cache, label, ns_uuid)
        if labels:
            project.ns_labels[ns_uuid] = labels

    def _update_default_virtual_network_perms2(self, ns_name, proj_uuid, oper='add'):
        if DBBaseKM.is_nested():
            return
        try:
            vn_fq_name = vnc_kube_config.cluster_default_pod_network_fq_name()
            pod_vn_obj = self._vnc_lib.virtual_network_read(fq_name=vn_fq_name)
            vn_fq_name = vnc_kube_config.cluster_default_service_network_fq_name()
            service_vn_obj = self._vnc_lib.virtual_network_read(fq_name=vn_fq_name)
        except NoIdError:
            return
        for vn_obj in [pod_vn_obj, service_vn_obj]:
            perms2 = vn_obj.perms2
            share = perms2.share
            tenant_found = False
            for item in share:
                if item.tenant == proj_uuid:
                    tenant_found = True
                    break
            if oper == 'add':
                if tenant_found:
                    continue
                else:
                    share_item = ShareType(tenant=proj_uuid, tenant_access=cfgm_common.PERMS_R)
                    share.append(share_item)
            else:
                share.remove(item)
            perms2.share = share
            vn_obj.perms2 = perms2
            self._vnc_lib.virtual_network_update(vn_obj)

    def _create_isolated_ns_virtual_network(
            self, ns_name, vn_name,
            vn_type, proj_obj, ipam_obj=None, provider=None,
            enforce_policy=False):
        """
        Create/Update a virtual network for this namespace.
        """
        vn_exists = False
        vn = VirtualNetwork(
            name=vn_name, parent_obj=proj_obj,
            virtual_network_properties=VirtualNetworkType(forwarding_mode='l3'),
            address_allocation_mode='flat-subnet-only')
        try:
            vn_obj = self._vnc_lib.virtual_network_read(
                fq_name=vn.get_fq_name())
            vn_exists = True
        except NoIdError:
            # VN does not exist. Create one.
            vn_obj = vn

        fabric_snat = False
        if vn_type == 'pod-network':
            if self._is_ip_fabric_snat_enabled(ns_name):
                fabric_snat = True

        if not vn_exists:
            # Add annotatins on this isolated virtual-network.
            VirtualNetworkKM.add_annotations(self, vn, namespace=ns_name,
                                             name=ns_name, isolated='True')
            # Instance-Ip for pods on this VN, should be allocated from
            # cluster pod ipam. Attach the cluster pod-ipam object
            # to this virtual network.
            vn_obj.add_network_ipam(ipam_obj, VnSubnetsType([]))
            if provider:
                # enable ip_fabric_forwarding
                vn_obj.add_virtual_network(provider)
            elif fabric_snat:
                # enable fabric_snat
                vn_obj.set_fabric_snat(True)
            else:
                # disable fabric_snat
                vn_obj.set_fabric_snat(False)
            vn_uuid = self._vnc_lib.virtual_network_create(vn_obj)
            # Cache the virtual network.
            VirtualNetworkKM.locate(vn_uuid)
        else:
            ip_fabric_enabled = False
            if provider:
                vn_refs = vn_obj.get_virtual_network_refs()
                ip_fabric_fq_name = provider.fq_name
                for vn in vn_refs or []:
                    vn_fq_name = vn['to']
                    if vn_fq_name == ip_fabric_fq_name:
                        ip_fabric_enabled = True
                        break
            if not ip_fabric_enabled and fabric_snat:
                # enable fabric_snat
                vn_obj.set_fabric_snat(True)
            else:
                # disable fabric_snat
                vn_obj.set_fabric_snat(False)
            # Update VN.
            self._vnc_lib.virtual_network_update(vn_obj)
            vn_uuid = vn_obj.get_uuid()

        vn_obj = self._vnc_lib.virtual_network_read(id=vn_uuid)

        # If required, enforce security policy at virtual network level.
        if enforce_policy:
            self._vnc_lib.set_tags(vn_obj, self._labels.get_labels_dict(VncSecurityPolicy.cluster_aps_uuid))

        return vn_obj

    def _delete_isolated_ns_virtual_network(self, ns_name, vn_name,
                                            proj_fq_name):
        """
        Delete the virtual network associated with this namespace.
        """
        # First lookup the cache for the entry.
        vn = VirtualNetworkKM.find_by_name_or_uuid(vn_name)
        if not vn:
            return

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
        except NoIdError:
            pass

        # Delete the network.
        self._vnc_lib.virtual_network_delete(id=vn.uuid)

        # Delete the network from cache.
        VirtualNetworkKM.delete(vn.uuid)

    def _attach_policy(self, vn_obj, *policies):
        for policy in policies or []:
            if policy:
                vn_obj.add_network_policy(
                    policy,
                    VirtualNetworkPolicyType(sequence=SequenceType(0, 0)))
        self._vnc_lib.virtual_network_update(vn_obj)
        for policy in policies or []:
            if policy:
                self._vnc_lib.ref_relax_for_delete(vn_obj.uuid, policy.uuid)

    def _create_policy_entry(self, src_vn_obj, dst_vn_obj):
        return PolicyRuleType(
            direction='<>',
            action_list=ActionListType(simple_action='pass'),
            protocol='any',
            src_addresses=[
                AddressType(virtual_network=src_vn_obj.get_fq_name_str())
            ],
            src_ports=[PortType(-1, -1)],
            dst_addresses=[
                AddressType(virtual_network=dst_vn_obj.get_fq_name_str())
            ],
            dst_ports=[PortType(-1, -1)])

    def _create_vn_vn_policy(self, policy_name, proj_obj, src_vn_obj, dst_vn_obj):
        policy_exists = False
        policy = NetworkPolicy(name=policy_name, parent_obj=proj_obj)
        try:
            policy_obj = self._vnc_lib.network_policy_read(
                fq_name=policy.get_fq_name())
            policy_exists = True
        except NoIdError:
            # policy does not exist. Create one.
            policy_obj = policy
        network_policy_entries = PolicyEntriesType()
        policy_entry = self._create_policy_entry(src_vn_obj, dst_vn_obj)
        network_policy_entries.add_policy_rule(policy_entry)
        policy_obj.set_network_policy_entries(network_policy_entries)
        if policy_exists:
            self._vnc_lib.network_policy_update(policy)
        else:
            self._vnc_lib.network_policy_create(policy)
        return policy_obj

    def _create_attach_policy(self, ns_name, proj_obj,
                              ip_fabric_vn_obj, pod_vn_obj, service_vn_obj):
        if not self._cluster_service_policy:
            cluster_service_np_fq_name = \
                vnc_kube_config.cluster_default_service_network_policy_fq_name()
            try:
                cluster_service_policy = self._vnc_lib. \
                    network_policy_read(fq_name=cluster_service_np_fq_name)
            except NoIdError:
                return
            self._cluster_service_policy = cluster_service_policy
        if not self._ip_fabric_policy:
            cluster_ip_fabric_np_fq_name = \
                vnc_kube_config.cluster_ip_fabric_policy_fq_name()
            try:
                cluster_ip_fabric_policy = self._vnc_lib. \
                    network_policy_read(fq_name=cluster_ip_fabric_np_fq_name)
            except NoIdError:
                return
            self._ip_fabric_policy = cluster_ip_fabric_policy

        self._nested_underlay_policy = None
        if DBBaseKM.is_nested() and not self._nested_underlay_policy:
            try:
                name = vnc_kube_config.cluster_nested_underlay_policy_fq_name()
                self._nested_underlay_policy = \
                    self._vnc_lib.network_policy_read(fq_name=name)
            except NoIdError:
                return

        policy_name = "-".join([vnc_kube_config.cluster_name(), ns_name, 'pod-service-np'])
        # policy_name = '%s-default' %ns_name
        ns_default_policy = self._create_vn_vn_policy(
            policy_name, proj_obj,
            pod_vn_obj, service_vn_obj)
        self._attach_policy(
            pod_vn_obj, ns_default_policy,
            self._ip_fabric_policy, self._cluster_service_policy,
            self._nested_underlay_policy)
        self._attach_policy(
            service_vn_obj, ns_default_policy,
            self._ip_fabric_policy, self._nested_underlay_policy)

    def _delete_policy(self, ns_name, proj_fq_name):
        policy_name = "-".join([vnc_kube_config.cluster_name(), ns_name, 'pod-service-np'])
        policy_fq_name = proj_fq_name[:]
        policy_fq_name.append(policy_name)
        try:
            self._vnc_lib.network_policy_delete(fq_name=policy_fq_name)
        except NoIdError:
            pass

    def _update_security_groups(self, ns_name, proj_obj):
        def _get_rule(ingress, sg, prefix, ethertype):
            sgr_uuid = str(uuid.uuid4())
            if sg:
                if ':' not in sg:
                    sg_fq_name = proj_obj.get_fq_name_str() + ':' + sg
                else:
                    sg_fq_name = sg
                addr = AddressType(security_group=sg_fq_name)
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

        # create default security group
        sg_name = vnc_kube_config.get_default_sg_name(ns_name)
        DEFAULT_SECGROUP_DESCRIPTION = "Default security group"
        id_perms = IdPermsType(enable=True,
                               description=DEFAULT_SECGROUP_DESCRIPTION)

        rules = []
        ingress = True
        egress = True
        if ingress:
            rules.append(_get_rule(True, None, '0.0.0.0', 'IPv4'))
            rules.append(_get_rule(True, None, '::', 'IPv6'))
        if egress:
            rules.append(_get_rule(False, None, '0.0.0.0', 'IPv4'))
            rules.append(_get_rule(False, None, '::', 'IPv6'))
        sg_rules = PolicyEntriesType(rules)

        sg_obj = SecurityGroup(name=sg_name, parent_obj=proj_obj,
                               id_perms=id_perms,
                               security_group_entries=sg_rules)

        SecurityGroupKM.add_annotations(self, sg_obj, namespace=ns_name,
                                        name=sg_obj.name,
                                        k8s_type=self._k8s_event_type)
        try:
            self._vnc_lib.security_group_create(sg_obj)
            self._vnc_lib.chown(sg_obj.get_uuid(), proj_obj.get_uuid())
        except RefsExistError:
            self._vnc_lib.security_group_update(sg_obj)
        sg = SecurityGroupKM.locate(sg_obj.get_uuid())
        return sg

    def vnc_namespace_add(self, namespace_id, name, labels):
        isolated_ns_ann = 'True' if self._is_namespace_isolated(name) \
            else 'False'

        # Check if policy enforcement is enabled at project level.
        # If not, then security will be enforced at VN level.
        if DBBaseKM.is_nested():
            # In nested mode, policy is always enforced at network level.
            # This is so that we do not enforce policy on other virtual
            # networks that may co-exist in the current project.
            secure_project = False
        else:
            secure_project = vnc_kube_config.is_secure_project_enabled()
        secure_vn = not secure_project

        proj_fq_name = vnc_kube_config.cluster_project_fq_name(name)
        proj_obj = Project(name=proj_fq_name[-1], fq_name=proj_fq_name)

        ProjectKM.add_annotations(self, proj_obj, namespace=name, name=name,
                                  k8s_uuid=(namespace_id),
                                  isolated=isolated_ns_ann)
        try:
            self._vnc_lib.project_create(proj_obj)
        except RefsExistError:
            proj_obj = self._vnc_lib.project_read(fq_name=proj_fq_name)
        project = ProjectKM.locate(proj_obj.uuid)

        # Validate the presence of annotated virtual network.
        ann_vn_fq_name = self._get_annotated_virtual_network(name)
        if ann_vn_fq_name:
            # Validate that VN exists.
            try:
                self._vnc_lib.virtual_network_read(ann_vn_fq_name)
            except NoIdError as e:
                self._logger.error(
                    "Unable to locate virtual network [%s]"
                    "annotated on namespace [%s]. Error [%s]" %
                    (ann_vn_fq_name, name, str(e)))

        # If this namespace is isolated, create it own network.
        if self._is_namespace_isolated(name) or name == 'default':
            vn_name = self._get_namespace_pod_vn_name(name)
            if self._is_ip_fabric_forwarding_enabled(name):
                ipam_fq_name = vnc_kube_config.ip_fabric_ipam_fq_name()
                ipam_obj = self._vnc_lib.network_ipam_read(fq_name=ipam_fq_name)
                provider = self._ip_fabric_vn_obj
            else:
                ipam_fq_name = vnc_kube_config.pod_ipam_fq_name()
                ipam_obj = self._vnc_lib.network_ipam_read(fq_name=ipam_fq_name)
                provider = None
            pod_vn = self._create_isolated_ns_virtual_network(
                ns_name=name, vn_name=vn_name, vn_type='pod-network',
                proj_obj=proj_obj, ipam_obj=ipam_obj, provider=provider,
                enforce_policy=secure_vn)
            # Cache pod network info in namespace entry.
            self._set_namespace_pod_virtual_network(name, pod_vn.get_fq_name())
            vn_name = self._get_namespace_service_vn_name(name)
            ipam_fq_name = vnc_kube_config.service_ipam_fq_name()
            ipam_obj = self._vnc_lib.network_ipam_read(fq_name=ipam_fq_name)
            service_vn = self._create_isolated_ns_virtual_network(
                ns_name=name, vn_name=vn_name, vn_type='service-network',
                ipam_obj=ipam_obj, proj_obj=proj_obj,
                enforce_policy=secure_vn)
            # Cache service network info in namespace entry.
            self._set_namespace_service_virtual_network(
                name, service_vn.get_fq_name())
            self._create_attach_policy(
                name, proj_obj,
                self._ip_fabric_vn_obj, pod_vn, service_vn)
        else:
            self._update_default_virtual_network_perms2(name, proj_obj.uuid)

        try:
            self._update_security_groups(name, proj_obj)
        except RefsExistError:
            pass

        if project:
            self._update_namespace_label_cache(labels, namespace_id, project)

            # If requested, enforce security policy at project level.
            if secure_project:
                proj_obj = self._vnc_lib.project_read(id=project.uuid)
                self._vnc_lib.set_tags(
                    proj_obj,
                    self._labels.get_labels_dict(
                        VncSecurityPolicy.cluster_aps_uuid))
        return project

    def vnc_namespace_delete(self, namespace_id, name):
        proj_fq_name = vnc_kube_config.cluster_project_fq_name(name)
        project_uuid = ProjectKM.get_fq_name_to_uuid(proj_fq_name)
        if not project_uuid:
            self._logger.error("Unable to locate project for k8s namespace "
                               "[%s]" % (name))
            return

        project = ProjectKM.get(project_uuid)
        if not project:
            self._logger.error("Unable to locate project for k8s namespace "
                               "[%s]" % (name))
            return

        # If the namespace is isolated, delete its virtual network.
        if self._is_namespace_isolated(name):
            self._delete_policy(name, proj_fq_name)
            vn_name = self._get_namespace_pod_vn_name(name)
            self._delete_isolated_ns_virtual_network(
                name, vn_name=vn_name, proj_fq_name=proj_fq_name)
            # Clear pod network info from namespace entry.
            self._set_namespace_pod_virtual_network(name, None)
            vn_name = self._get_namespace_service_vn_name(name)
            self._delete_isolated_ns_virtual_network(
                name, vn_name=vn_name, proj_fq_name=proj_fq_name)
            # Clear service network info from namespace entry.
            self._set_namespace_service_virtual_network(name, None)
        else:
            self._update_default_virtual_network_perms2(name, project_uuid, oper='del')

        # delete security groups
        security_groups = project.get_security_groups()
        for sg_uuid in security_groups:
            sg = SecurityGroupKM.get(sg_uuid)
            if not sg:
                continue
            sg_name = vnc_kube_config.get_default_sg_name(name)
            if sg.name != sg_name:
                continue
            for vmi_id in list(sg.virtual_machine_interfaces):
                try:
                    self._vnc_lib.ref_update(
                        'virtual-machine-interface', vmi_id,
                        'security-group', sg.uuid, None, 'DELETE')
                except NoIdError:
                    pass
            self._vnc_lib.security_group_delete(id=sg_uuid)

        # delete the label cache
        if project:
            self._clear_namespace_label_cache(namespace_id, project)
        # delete the namespace
        self._delete_namespace(name)

        # If project was created for this namesspace, delete the project.
        if vnc_kube_config.get_project_name_for_namespace(name) == project.name:
            self._vnc_lib.project_delete(fq_name=proj_fq_name)

    def _sync_namespace_project(self):
        """Sync vnc project objects with K8s namespace object.

        This method walks vnc project local cache and validates that
        a kubernetes namespace object exists for this project.
        If a kubernetes namespace object is not found for this project,
        then construct and simulates a delete event for the namespace,
        so the vnc project can be cleaned up.
        """
        for project in ProjectKM.objects():
            if project.owner != 'k8s' or project.cluster != vnc_kube_config.cluster_name():
                continue
            k8s_namespace_uuid = project.get_k8s_namespace_uuid()
            # Proceed only if this project is tagged with a k8s namespace.
            if k8s_namespace_uuid and not self._get_namespace(k8s_namespace_uuid):
                event = {}
                dict_object = {}
                dict_object['kind'] = 'Namespace'
                dict_object['metadata'] = {}
                dict_object['metadata']['uid'] = k8s_namespace_uuid
                dict_object['metadata']['name'] = project.get_k8s_namespace_name()

                event['type'] = 'DELETED'
                event['object'] = dict_object
                self._queue.put(event)

    def namespace_timer(self):
        self._sync_namespace_project()

    def _get_namespace_firewall_ingress_rule_name(self, ns_name):
        return "-".join([vnc_kube_config.cluster_name(),
                         self._k8s_event_type, ns_name, "ingress"])

    def _get_namespace_firewall_egress_rule_name(self, ns_name):
        return "-".join([vnc_kube_config.cluster_name(),
                         self._k8s_event_type, ns_name, "egress"])

    def add_namespace_security_policy(self, k8s_namespace_uuid):
        """
        Create a firwall rule for default behavior on a namespace.
        """
        ns = self._get_namespace(k8s_namespace_uuid)

        if not ns:
            return

        # Add custom namespace label on the namespace object.
        self._labels.append(
            k8s_namespace_uuid,
            self._labels.get_namespace_label(ns.name))

        if not ns.firewall_ingress_allow_rule_uuid:
            ingress_rule_name = self._get_namespace_firewall_ingress_rule_name(ns.name)

            # Create a rule for default allow behavior on this namespace.
            ns.firewall_ingress_allow_rule_uuid =\
                VncSecurityPolicy.create_firewall_rule_allow_all(
                    ingress_rule_name,
                    self._labels.get_namespace_label(ns.name))

            # Add default allow rule to the "global allow" firewall policy.
            VncSecurityPolicy.add_firewall_rule(
                VncSecurityPolicy.allow_all_fw_policy_uuid,
                ns.firewall_ingress_allow_rule_uuid)

        if not ns.firewall_egress_allow_rule_uuid:

            egress_rule_name = self._get_namespace_firewall_egress_rule_name(ns.name)

            # Create a rule for default egress allow behavior on this namespace.
            ns.firewall_egress_allow_rule_uuid =\
                VncSecurityPolicy.create_firewall_rule_allow_all(
                    egress_rule_name, {},
                    self._labels.get_namespace_label(ns.name))

            # Add default egress allow rule to "global allow" firewall policy.
            VncSecurityPolicy.add_firewall_rule(
                VncSecurityPolicy.allow_all_fw_policy_uuid,
                ns.firewall_egress_allow_rule_uuid)

    def delete_namespace_security_policy(self, ns_name):
        """
        Delete firwall rule created to enforce default behavior on this
        namespace.
        """
        if VncSecurityPolicy.allow_all_fw_policy_uuid:
            # Dis-associate and delete the ingress rule from namespace policy.
            rule_name = self._get_namespace_firewall_ingress_rule_name(ns_name)
            rule_uuid = VncSecurityPolicy.get_firewall_rule_uuid(rule_name)
            VncSecurityPolicy.delete_firewall_rule(
                VncSecurityPolicy.allow_all_fw_policy_uuid, rule_uuid)

            # Dis-associate and delete egress rule from namespace policy.
            egress_rule_name = self._get_namespace_firewall_egress_rule_name(ns_name)
            egress_rule_uuid = VncSecurityPolicy.get_firewall_rule_uuid(egress_rule_name)
            VncSecurityPolicy.delete_firewall_rule(
                VncSecurityPolicy.allow_all_fw_policy_uuid, egress_rule_uuid)

    def process(self, event):
        event_type = event['type']
        kind = event['object'].get('kind')
        name = event['object']['metadata'].get('name')
        ns_id = event['object']['metadata'].get('uid')
        labels = dict(event['object']['metadata'].get('labels', {}))
        print(
            "%s - Got %s %s %s:%s"
            % (self._name, event_type, kind, name, ns_id))
        self._logger.debug(
            "%s - Got %s %s %s:%s"
            % (self._name, event_type, kind, name, ns_id))

        if event['type'] == 'ADDED' or event['type'] == 'MODIFIED':

            # Process label add.
            # We implicitly add a namespace label as well.
            labels['namespace'] = name
            self._labels.process(ns_id, labels)

            self.vnc_namespace_add(ns_id, name, labels)
            self.add_namespace_security_policy(ns_id)

            if event['type'] == 'MODIFIED' and self._get_namespace(name):
                # If labels on this namespace has changed, update the pods
                # on this namespace with current namespace labels.
                added_labels, removed_labels =\
                    self._get_namespace(name).get_changed_labels()
                namespace_pods = PodKM.get_namespace_pods(name)

                # Remove the old label first.
                #
                # 'Remove' must be done before 'Add', to account for the case
                # where, what got changed was the value for an existing label.
                # This is especially important as, remove label code only
                # considers the key while deleting the label.
                #
                # If Add is done before Remove, then the updated label that
                # was set by 'Add', will be deleted by the 'Remove' call.
                if removed_labels:
                    VncPod.remove_labels(namespace_pods, removed_labels)
                if added_labels:
                    VncPod.add_labels(namespace_pods, added_labels)

        elif event['type'] == 'DELETED':
            self.delete_namespace_security_policy(name)
            # Delete label deletes for this namespace.
            self._labels.process(ns_id)
            self.vnc_namespace_delete(ns_id, name)

        else:
            self._logger.warning(
                'Unknown event type: "{}" Ignoring'.format(event['type']))
