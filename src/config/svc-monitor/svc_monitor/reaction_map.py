REACTION_MAP = {
    "service_appliance_set": {
        'self': [],
        'service_appliance': []
    },
    "service_appliance": {
        'self': ['service_appliance_set','physical_interface'],
        'service_appliance_set': []
    },
    "loadbalancer_pool": {
        'self': ['service_instance'],
        'virtual_ip': [],
        'service_instance': [],
        'loadbalancer_listener': [],
        'loadbalancer_member': [],
        'loadbalancer_healthmonitor': [],
    },
    "loadbalancer_member": {
        'self': ['loadbalancer_pool'],
        'loadbalancer_pool': []
    },
    "virtual_ip": {
        'self': ['loadbalancer_pool'],
        'loadbalancer_pool': []
    },
    "loadbalancer_listener": {
        'self': ['loadbalancer_pool'],
        'loadbalancer_pool': [],
        'loadbalancer': []
    },
    "loadbalancer": {
        'self': ['loadbalancer_listener'],
        'loadbalancer_listener': []
    },
    "loadbalancer_healthmonitor": {
        'self': ['loadbalancer_pool'],
        'loadbalancer_pool': []
    },
    "service_instance": {
        'self': ['virtual_machine', 'port_tuple','instance_ip'],
        'loadbalancer_pool': [],
        'virtual_machine': [],
        'port_tuple': [],
        'virtual_machine_interface' : []
    },
    "instance_ip": {
        'self': [],
        'service_instance': [],
        'virtual_machine_interface': [],
    },
    "floating_ip": {
        'self': [],
    },
    "security_group": {
        'self': [],
        'virtual_machine_interface': [],
    },
    "service_template": {
        'self': [],
    },
    "physical_router": {
        'self': [],
    },
    "physical_interface": {
        'self': [],
        'service_appliance':['virtual_machine_interface'],
        'virtual_machine_interface':['service_appliance'],
    },
    "logical_interface": {
        'self': [],
    },
    "virtual_network": {
        'self': [],
    },
    "virtual_machine": {
        'self': ['virtual_machine_interface'],
        'service_instance': [],
        'virtual_machine_interface': [],
    },
    "port_tuple": {
        'self': ['virtual_machine_interface'],
        'service_instance': [],
        'virtual_machine_interface': [],
    },
    "virtual_machine_interface": {
        'self': ['interface_route_table', 'virtual_machine',
                 'port_tuple', 'security_group',
                 'instance_ip'],
        'interface_route_table': [],
        'security_group': [],
        'virtual_machine': [],
        'port_tuple': ['physical_interface', 'instance_ip'],
        'physical_interface': ['service_instance']
    },
    "interface_route_table": {
        'self': [],
        'virtual_machine_interface': [],
    },
    "project": {
        'self': [],
    },
    "logical_router": {
        'self': [],
    },
}

