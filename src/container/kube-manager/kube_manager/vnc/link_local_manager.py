#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#

"""
Link-local service management functions.
"""

from vnc_api.vnc_api import *

def create_link_local_service_entry(vnc_lib, name, service_ip, service_port,
        fabric_ip, fabric_port, fabric_dns_svc_name=""):
    """
    Create a link local service in vrouter for the specified Service IP.
    """

    # Create a link-local service entry.
    linklocal_obj=LinklocalServiceEntryType(
        linklocal_service_name=name, linklocal_service_ip=service_ip,
        linklocal_service_port=service_port,
        ip_fabric_service_ip=[fabric_ip],
        ip_fabric_service_port=fabric_port,
        ip_fabric_DNS_service_name=fabric_dns_svc_name)

    # Get current VRouter config from API server.
    try:
        current_config=vnc_lib.global_vrouter_config_read(
            fq_name=['default-global-system-config',
                'default-global-vrouter-config'])
    except NoIdError:
        # VRouter config does not exist. Create one.
        linklocal_services_obj=LinklocalServicesTypes([linklocal_obj])
        conf_obj=GlobalVrouterConfig(linklocal_services=linklocal_services_obj)
        result=vnc_lib.global_vrouter_config_create(conf_obj)
        return
    except:
        # An exception occurred. Throw it to the caller.
        raise

    # Get currently configured link-local services.
    current_linklocal=current_config.get_linklocal_services()
    if current_linklocal is None:
        # No link-local services have been configured yet.
        obj = {'linklocal_service_entry': []}
    else:
        # Get the currently configured link-local services.
        obj = current_linklocal.__dict__

    new_linklocal=[]
    key_ll_svc_entry = 'linklocal_service_entry'
    value = obj[key_ll_svc_entry]
    found=False
    for vl in value:
        entry = vl.__dict__
        if ('linklocal_service_name' in entry and
            entry['linklocal_service_name'] == name):
            # An entry with the service name exists. Replace it
            # with the new/latest object.
            new_linklocal.append(linklocal_obj)
            found=True
        else:
            new_linklocal.append(vl)

    if not found:
        # An entry with requested name was not found.
        # Append the new object.
        new_linklocal.append(linklocal_obj)

    obj[key_ll_svc_entry] = new_linklocal
    conf_obj=GlobalVrouterConfig(linklocal_services=obj)

    # Update API server with new link-local service info.
    try:
        vnc_lib.global_vrouter_config_update(conf_obj)
    except:
        # Update failed with an exception. Throw it to the caller.
        raise

def delete_link_local_service_entry(vnc_lib, name):
    # Get current VRouter config from API server.
    try:
        current_config=vnc_lib.global_vrouter_config_read(
            fq_name=['default-global-system-config',
                'default-global-vrouter-config'])
    except NoIdError:
        # VRoute config not found. Nothing to delete.
        return
    except:
        # An exception occurred while lookup. Throw it to caller.
        raise

    # Get currently configured link-local services.
    current_linklocal=current_config.get_linklocal_services()
    if current_linklocal is None:
        obj = {'linklocal_service_entry': []}
    else:
        obj = current_linklocal.__dict__

    key_ll_svc_entry = 'linklocal_service_entry'
    value = obj[key_ll_svc_entry]
    new_linklocal=[]
    for vl in value:
        entry = vl.__dict__
        # Skip matching entry and build a new list with remaining entries.
        if ('linklocal_service_name' in entry and
            entry['linklocal_service_name'] != name):
                new_linklocal.append(vl)

    obj[key_ll_svc_entry] = new_linklocal
    conf_obj=GlobalVrouterConfig(linklocal_services=obj)

    # Update API server with new link-local service info.
    try:
        vnc_lib.global_vrouter_config_update(conf_obj)
    except:
        # Update failed with an exception. Throw it to the caller.
        raise
