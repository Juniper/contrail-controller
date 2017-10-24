#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#

"""
Flow aging service management functions.
"""

from vnc_api.vnc_api import *
from vnc_kubernetes_config import VncKubernetesConfig as vnc_kube_config

def create_flow_aging_timeout_entry(vnc_lib, protocol, port,
        timeout_in_seconds):
    """
    Create a flow-aging entry in vrouter for a specific flow.
    """
    flow_aging_obj=FlowAgingTimeout(protocol, port, timeout_in_seconds)
    flow_aging_list=FlowAgingTimeoutList([flow_aging_obj])

    # Get current VRouter config from API server.
    try:
        current_config=vnc_lib.global_vrouter_config_read(
            fq_name=['default-global-system-config',
                'default-global-vrouter-config'])
    except NoIdError:
        # VRouter config does not exist. Create one.
        conf_obj=GlobalVrouterConfig(flow_aging_timeout_list=flow_aging_list)
        result=vnc_lib.global_vrouter_config_create(conf_obj)
        return

    # Get currently configured flow aging timeouts.
    current_flow_aging_list = current_config.get_flow_aging_timeout_list()
    if current_flow_aging_list:
        # Get the currently configured link-local services.
        found = False
        for flow in current_flow_aging_list.get_flow_aging_timeout():
            if str(flow.get_protocol()) == protocol and\
               str(flow.get_port()) == port:
                flow.set_timeout_in_seconds(timeout_in_seconds)
                found = True
        if not found:
            current_flow_aging_list.add_flow_aging_timeout(flow_aging_obj)
    else:
        current_flow_aging_list = flow_aging_list

    current_config.set_flow_aging_timeout_list(current_flow_aging_list)

    # Update API server with new flow-aging service info.
    vnc_lib.global_vrouter_config_update(current_config)
