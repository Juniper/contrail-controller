#
# Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
#

"""Base class for feature plugins."""

import abc
from builtins import object
from builtins import str
from collections import OrderedDict
import copy

from abstract_device_api.abstract_device_xsd import *
from attrdict import AttrDict

from .db import BgpRouterDM, GlobalVRouterConfigDM, LogicalRouterDM, \
    PhysicalInterfaceDM, RoutingInstanceDM, VirtualMachineInterfaceDM, \
    VirtualNetworkDM, VirtualPortGroupDM
from .imports import import_feature_plugins


class abstractclassmethod(classmethod):
    __isabstractmethod__ = True

    def __init__(self, callable):
        callable.__isabstractmethod__ = True
        super(abstractclassmethod, self).__init__(callable)
# end abstractclassmethod


class FeatureBase(object):
    """Base class for feature plugins."""

    _plugins = {}

    class PluginRegistrationFailed(Exception):
        """Exception class to store plugin registration exceptions."""

        def __init__(self, exceptions):
            """Exception initializer."""
            self.exceptions = exceptions
        # end __init__

        def __str__(self):
            """Convert exception to string for logging."""
            ex_msg = "Feature plugin registration failed:\n"
            for ex in self.exceptions or []:
                ex_msg += ex + "\n"
            return ex_msg
        # end __str__
    # end PluginRegistrationFailed

    @classmethod
    def register_plugins(cls):
        cls._plugins = {}
        # make sure modules are loaded
        import_feature_plugins()
        # register plugins
        # find all leaf implementation classes derived from this class
        subclasses = set()
        work = [cls]
        while work:
            parent = work.pop()
            if not parent.__subclasses__():
                subclasses.add(parent)
                continue
            for child in parent.__subclasses__():
                if child not in subclasses:
                    work.append(child)
        # register all plugins,
        # if there is any exception, continue to register all other plugins,
        # finally throw one single exception to the caller
        exceptions = []
        for scls in subclasses or []:
            try:
                feature_name = scls.feature_name()
                if feature_name in cls._plugins:
                    exceptions.append(
                        "Duplicate plugin found for feature %s" % feature_name)
                else:
                    cls._plugins[feature_name] = scls
            except Exception as e:
                exceptions.append(str(e))
        if exceptions:
            raise PluginRegistrationFailed(exceptions)
    # end register_plugins

    @classmethod
    def plugins(cls, logger, pr):
        features = pr.get_features()
        if not features:
            logger.warning("No features found for %s(%s)" % (pr.name, pr.uuid))
        plugins = []
        for feature in features:
            plugin_cls = cls._plugins.get(feature.feature.name)
            if plugin_cls:
                logger.info("Found feature plugin for pr=%s(%s), feature=%s" %
                            (pr.name, pr.uuid, feature.feature.name))
                plugin = plugin_cls(logger, pr, feature.configs)
                plugins.append(plugin)
            else:
                logger.warning("No plugin found for feature %s" %
                               feature.feature.name)
        return plugins
    # end plugins

    @abstractclassmethod
    def feature_name(cls):
        pass
    # end feature_name

    @staticmethod
    def _add_to_list(lst, value):
        if value not in lst:
            lst.append(value)
    # end _add_to_list

    @staticmethod
    def _add_ref_to_list(lst, value):
        if not any(v.get_name() == value for v in lst):
            lst.append(Reference(name=value))
    # end _add_ref_to_list

    @classmethod
    def _add_ip_address(cls, unit, address, gateway=None):
        if ':' in address:
            family = 'inet6'
            if gateway == '0:0:0:0:0:0:0:0' or gateway == '::/0':
                gateway = None
        else:
            family = 'inet'
            if gateway == '0.0.0.0':
                gateway = None
        ip_address = IpAddress(address=address, family=family,
                               gateway=gateway)
        cls._add_to_list(unit.get_ip_addresses(), ip_address)
    # end _add_ip_address

    def __init__(self, logger, physical_router, configs):
        self._logger = logger
        self._physical_router = physical_router
        self._configs = configs
    # end __init__

    def verify_plugin(self):
        return False
    # end verify_plugin

    @abc.abstractmethod
    def feature_config(self, **kwargs):
        pass
    # end feature_config

    @staticmethod
    def _add_or_lookup_pi(pi_map, name, interface_type=None):
        if name in pi_map:
            pi, li_map = pi_map[name]
            if interface_type:
                pi.set_interface_type(interface_type)
        else:
            pi = PhysicalInterface(name=name, interface_type=interface_type)
            li_map = OrderedDict()
            pi_map[name] = (pi, li_map)
        return pi, li_map
    # end _add_or_lookup_pi

    @staticmethod
    def _add_or_lookup_li(li_map, name, unit):
        if name in li_map:
            li = li_map[name]
        else:
            li = LogicalInterface(name=name, unit=unit)
            li_map[name] = li
        return li
    # end _add_or_lookup_li

    @staticmethod
    def _is_family_configured(params, family_name):
        if params is None or params.get('address_families') is None:
            return False
        families = params['address_families'].get('family', [])
        if family_name in families:
            return True
        return False
    # end _is_family_configured

    @classmethod
    def _is_evpn(cls, physical_router):
        bgp_router = BgpRouterDM.get(physical_router.bgp_router)
        if not bgp_router:
            return False
        return cls._is_family_configured(bgp_router.params, 'e-vpn')
    # end _is_evpn

    @classmethod
    def _is_enterprise_style(cls, physical_router):
        if physical_router.fabric_obj:
            return physical_router.fabric_obj.enterprise_style
    # end _is_enterprise_style

    @staticmethod
    def _get_primary_ri(vn_obj):
        for ri_id in vn_obj.routing_instances:
            ri_obj = RoutingInstanceDM.get(ri_id)
            if ri_obj is not None and ri_obj.fq_name[-1] == vn_obj.fq_name[-1]:
                return ri_obj
        return None
    # end _get_primary_ri

    @staticmethod
    def _get_encapsulation_priorities():
        return GlobalVRouterConfigDM.global_encapsulation_priorities or \
            ['MPLSoGRE']
    # end _get_encapsulation_priorities

    @staticmethod
    def _get_subnet_for_cidr(cidr):
        cidr_parts = cidr.split('/', 1)
        return Subnet(prefix=cidr_parts[0],
                      prefix_len=int(cidr_parts[1]) if len(cidr_parts) > 1
                      else 32)
    # end _get_subnet_for_cidr

    @staticmethod
    def _get_route_for_cidr(cidr):
        cidr_parts = cidr.split('/', 1)
        return Route(prefix=cidr_parts[0],
                     prefix_len=int(cidr_parts[1]) if len(cidr_parts) > 1
                     else 32)
    # end _get_route_for_cidr

    @staticmethod
    def _get_export_import_targets(vn_obj, ri_obj):
        if vn_obj.route_targets:
            export_targets = vn_obj.route_targets & ri_obj.export_targets
            import_targets = vn_obj.route_targets & ri_obj.import_targets
        else:
            export_targets = copy.copy(ri_obj.export_targets)
            import_targets = copy.copy(ri_obj.import_targets)
        for linked_ri_id in ri_obj.routing_instances:
            linked_ri = RoutingInstanceDM.get(linked_ri_id)
            if linked_ri is None:
                continue
            import_targets |= linked_ri.export_targets
        return export_targets, import_targets
    # end _get_export_import_targets

    @staticmethod
    def _is_valid_vn(vn_uuid, mode):
        vn = VirtualNetworkDM.get(vn_uuid)
        return vn is not None and vn.vn_network_id is not None and \
            vn.get_vxlan_vni() is not None and \
            vn.get_forwarding_mode() is not None and \
            mode in vn.get_forwarding_mode()
    # end _is_valid_vn

    def _get_connected_vns(self, mode):
        vns = set()
        for lr_uuid in self._physical_router.logical_routers or []:
            lr = LogicalRouterDM.get(lr_uuid)
            if not lr:
                continue
            vns = vns.union(lr.get_connected_networks(
                include_internal=False,
                pr_uuid=self._physical_router.uuid))
        for vn_uuid in self._physical_router.virtual_networks:
            vns.add(vn_uuid)
            vn_obj = VirtualNetworkDM.get(vn_uuid)
            if vn_obj and vn_obj.router_external:
                pvn_list = vn_obj.get_connected_private_networks() or []
                vns = vns.union(pvn_list)

        return [vn for vn in list(vns) if self._is_valid_vn(vn, mode)]
    # end _get_connected_vns

    def _get_vn_li_map(self, mode):
        vn_dict = OrderedDict()
        for vpg_uuid in self._physical_router.virtual_port_groups or []:
            vpg_obj = VirtualPortGroupDM.get(vpg_uuid)
            if not vpg_obj:
                continue
            vpg_interfaces = vpg_obj.physical_interfaces
            for vmi_uuid in vpg_obj.virtual_machine_interfaces:
                vmi_obj = VirtualMachineInterfaceDM.get(vmi_uuid)
                if not vmi_obj or not vmi_obj.virtual_network:
                    continue
                if not self._is_valid_vn(vmi_obj.virtual_network, mode):
                    continue
                vn_obj = VirtualNetworkDM.get(vmi_obj.virtual_network)
                vlan_tag = vmi_obj.vlan_tag
                port_vlan_tag = vmi_obj.port_vlan_tag
                for pi_uuid in vpg_interfaces:
                    if pi_uuid not in \
                            self._physical_router.physical_interfaces:
                        continue
                    ae_id = vpg_obj.pi_ae_map.get(pi_uuid)
                    if ae_id is not None:
                        pi_name = 'ae' + str(ae_id)
                    else:
                        pi_obj = PhysicalInterfaceDM.get(pi_uuid)
                        if not pi_obj:
                            continue
                        pi_name = pi_obj.name
                    unit = str(vlan_tag)
                    vn_dict.setdefault(vn_obj.uuid, []).append(
                        AttrDict(pi_name=pi_name, li_name=pi_name + '.' + unit,
                                 unit=unit, vlan_tag=vlan_tag,
                                 port_vlan_tag=port_vlan_tag,
                                 vpg_obj=vpg_obj))
                    # In VPG we will have either regular interface or ae. if
                    #  multiple vpg_interfaces means it's LAG or MH, so one
                    # is sufficient.
                    break
        return vn_dict
    # end _get_vn_li_map

# end FeatureBase
