REACTION_MAP = {
    "service_appliance_set": {
        'self': [],
        'service_appliance': ['service_template']
    },
    "service_appliance": {
        'self': ['service_appliance_set','physical_interface'],
        'service_appliance_set': []
    },
    "loadbalancer_healthmonitor": {
        'self': ['loadbalancer_pool'],
    },
    "loadbalancer_member": {
        'self': ['loadbalancer_pool'],
    },
    "virtual_ip": {
        'self': ['loadbalancer_pool'],
    },
    "loadbalancer_pool": {
        'self': ['loadbalancer_listener'],
        'virtual_ip': [],
        'loadbalancer_member': ['loadbalancer_listener'],
        'loadbalancer_healthmonitor': ['loadbalancer_listener'],
    },
    "loadbalancer_listener": {
        'self': ['loadbalancer'],
        'loadbalancer_pool': ['loadbalancer']
    },
    "loadbalancer": {
        'self': [],
        'loadbalancer_listener': [],
    },
    "service_instance": {
        'self': ['virtual_machine', 'port_tuple','instance_ip'],
        'virtual_machine': [],
        'port_tuple': [],
        'virtual_machine_interface' : [],
        'service_health_check': [],
        'interface_route_table': [],
        'service_template': [],
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
        'service_appliance_set':['service_instance'],
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
        'service_health_check': [],
        'interface_route_table': [],
    },
    "virtual_machine_interface": {
        'self': ['interface_route_table', 'virtual_machine',
                 'port_tuple', 'security_group',
                 'instance_ip', 'service_health_check'],
        'interface_route_table': [],
        'service_health_check': [],
        'security_group': [],
        'virtual_machine': [],
        'port_tuple': ['physical_interface', 'instance_ip'],
        'physical_interface': ['service_instance']
    },
    "interface_route_table": {
        'self': ['service_instance'],
        'virtual_machine_interface': [],
        'service_instance': [],
    },
    "service_health_check": {
        'self': ['service_instance'],
        'virtual_machine_interface': [],
        'service_instance': [],
    },
    "project": {
        'self': [],
    },
    "logical_router": {
        'self': [],
    },
}
