#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#

REACTION_MAP = {
    "instance_ip": {
        'self': ['virtual_machine_interface', 'floating_ip'],
        'virtual_machine_interface': [],
        'floating_ip': [],
    },
    "floating_ip": {
        'self': ['virtual_machine_interface', 'instance_ip'],
        'virtual_machine_interface': [],
        'instance_ip': [],
    },
    "security_group": {
        'self': [],
        'virtual_machine_interface': [],
    },
    "virtual_network": {
        'self': [],
    },
    "virtual_machine": {
        'self': ['virtual_machine_interface'],
        'virtual_machine_interface': [],
    },
    "virtual_machine_interface": {
        'self': ['virtual_machine', 'security_group',
                 'instance_ip', 'floating_ip'],
        'security_group': [],
        'virtual_machine': [],
        'instance_ip': [],
        'floating_ip': [],
    },
    "project": {
        'self': [],
    },
}
