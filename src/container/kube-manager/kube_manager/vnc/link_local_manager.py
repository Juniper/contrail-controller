#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#

"""
Link-local service management functions.
"""
import socket

from cfgm_common.exceptions import NoIdError
from vnc_api.gen.resource_xsd import (
    LinklocalServiceEntryType, LinklocalServicesTypes
)
from vnc_api.gen.resource_client import GlobalVrouterConfig

from kube_manager.vnc.vnc_kubernetes_config import VncKubernetesConfig as vnc_kube_config


def _get_linklocal_entry_name(name, k8s_ns):
    if not k8s_ns:
        project_fq_name = vnc_kube_config.cluster_default_project_fq_name()
    else:
        project_fq_name = vnc_kube_config.cluster_project_fq_name(k8s_ns)
    ll_name = project_fq_name + [name]
    return "-".join(ll_name)


def create_link_local_service_entry(
        vnc_lib, name, service_ip, service_port,
        fabric_ip, fabric_port, fabric_dns_svc_name="", k8s_ns=None):
    """
    Create a link local service in vrouter for the specified Service IP.
    """

    link_local_name = _get_linklocal_entry_name(name, k8s_ns)

    """
    check if fabric_ip is a valid ip address. If not, assume it is a
    hostname or fqdn.
    """

    try:
        socket.inet_aton(fabric_ip)
    except socket.error:
        fabric_dns_svc_name = fabric_ip
        fabric_ip = ""

    # Create a link-local service entry.
    linklocal_obj = LinklocalServiceEntryType(
        linklocal_service_name=link_local_name, linklocal_service_ip=service_ip,
        linklocal_service_port=service_port,
        ip_fabric_service_ip=[fabric_ip],
        ip_fabric_service_port=fabric_port,
        ip_fabric_DNS_service_name=fabric_dns_svc_name)

    # Get current VRouter config from API server.
    try:
        current_config = vnc_lib.global_vrouter_config_read(
            fq_name=['default-global-system-config',
                     'default-global-vrouter-config'])
    except NoIdError:
        # VRouter config does not exist. Create one.
        linklocal_services_obj = LinklocalServicesTypes([linklocal_obj])
        conf_obj = GlobalVrouterConfig(linklocal_services=linklocal_services_obj)
        vnc_lib.global_vrouter_config_create(conf_obj)
        return

    # Get currently configured link-local services.
    current_linklocal = current_config.get_linklocal_services()
    if current_linklocal is None:
        # No link-local services have been configured yet.
        obj = {'linklocal_service_entry': []}
    else:
        # Get the currently configured link-local services.
        obj = current_linklocal.__dict__

    new_linklocal = []
    key_ll_svc_entry = 'linklocal_service_entry'
    value = obj[key_ll_svc_entry]
    found = False
    for vl in value:
        entry = vl.__dict__
        if ('linklocal_service_name' in entry and entry['linklocal_service_name'] == link_local_name):
            # An entry with the service name exists. Replace it
            # with the new/latest object.
            new_linklocal.append(linklocal_obj)
            found = True
        else:
            new_linklocal.append(vl)

    if not found:
        # An entry with requested name was not found.
        # Append the new object.
        new_linklocal.append(linklocal_obj)

    obj[key_ll_svc_entry] = new_linklocal
    conf_obj = GlobalVrouterConfig(linklocal_services=obj)

    # Update API server with new link-local service info.
    vnc_lib.global_vrouter_config_update(conf_obj)


def delete_link_local_service_entry(vnc_lib, name, k8s_ns=None):
    link_local_name = _get_linklocal_entry_name(name, k8s_ns)

    # Get current VRouter config from API server.
    try:
        current_config = vnc_lib.global_vrouter_config_read(
            fq_name=['default-global-system-config',
                     'default-global-vrouter-config'])
    except NoIdError:
        # VRoute config not found. Nothing to delete.
        return

    # Get currently configured link-local services.
    current_linklocal = current_config.get_linklocal_services()
    if current_linklocal is None:
        obj = {'linklocal_service_entry': []}
    else:
        obj = current_linklocal.__dict__

    key_ll_svc_entry = 'linklocal_service_entry'
    value = obj[key_ll_svc_entry]
    new_linklocal = []
    for vl in value:
        entry = vl.__dict__
        # Skip matching entry and build a new list with remaining entries.
        if ('linklocal_service_name' in entry and entry['linklocal_service_name'] != link_local_name):
            new_linklocal.append(vl)

    obj[key_ll_svc_entry] = new_linklocal
    conf_obj = GlobalVrouterConfig(linklocal_services=obj)

    # Update API server with new link-local service info.
    vnc_lib.global_vrouter_config_update(conf_obj)
