# 1. Introduction

Contrail controller container solution is deployed as the following 4 containers
- controller
  This consists of following services
  - config
  - control
  - webui
- analytics
- analyticsdb
- vrouter-agent

A typical HA solution will have 3 hosts each having controller, analytics & analyticsdb containers on it.

# 2. Problem statement
Contrail Analytics should be installable as a standalone product independent of Contrail Controller SDN solution. Standalone Contrail Analytics should have config, webui, analytics and analyticsdb services. analytics and analyticsdb have their own containers. config & webui will come from controller container, but this requires controller container to be deployed with control service disabled.

# 3. Proposed solution

## 3.1 Design
Add configuration option in the ansible inventory file such that
- No extra configuration should be needed if user wants to deploy controller with default solution i.e. all 3 services - config, control and webui are enabled in the controller container.
- On a controller container if any service is enabled explicitly, rest of the services not enabled explicitly should stay disabled.
- Config service is mandatory on a node or cluster. It is left upto the user to make sure atleast one controller container has config service enabled.
- After controller container is up, a disabled service could be started/stopped manually.
- Generic framework to be set in place such that config, control and webui services can be independently enabled/disabled for any controller container.

## 3.2 Alternatives considered
Create another container, analytics+ container consisting of config, webui and analytics. For analytics-only solution, we would need analytics+ and analyticsdb containers.
This alternative was dropped as we didnt see much value is having another overlapping container. The same is being achieved by having 3 containers listed in proposed solution and
enabling/disabling the individual services in controller container as needed.

## 3.3 User workflow impact
Describe how users will use the feature.

## 3.4 UI changes
We have to make sure non existence of control and vrouter services will not create issues on the UI

## 3.5 Notification impact
None

# 4. Implementation
## 4.1 Node role ansible scripts
Read the inventory file for [contrail-controller] hosts. List of IP addresses is controller_list. If controller-components are not defined or empty, set config_server_list, control_server_list and webui_server_list as controller_list. If controller_components is non-emtpy, assign IP addresses to the respective lists as per configuration. Pass these parameters to controller containers.

## 4.2 Contrail role - config, control, webui service ansible scripts
In controller container, start the services only if they is enabled. As rabbitmq, cassandra and zookeeper are used by config only, they are enabled only if config service is enabled.

# 5. Performance and scaling impact
## 5.1 API and control plane
None

## 5.2 Forwarding performance
None

# 6. Upgrade
None

# 7. Deprecations
None

# 8. Dependencies
None

# 9. Testing
## 9.1 Unit tests
### 9.1.1 default inventory file configuration for controller container

```
; single node
[contrail-controllers]
10.84.32.10

[contrail-analyticsdb]
10.84.32.10

[contrail-analytics]
10.84.32.10
```

```
; multi node cluster
[contrail-controllers]
10.84.32.10
10.84.32.11
10.84.32.12

[contrail-analyticsdb]
10.84.32.10
10.84.32.11
10.84.32.12

[contrail-analytics]
10.84.32.10
10.84.32.11
10.84.32.12
```

### 9.1.2 analytics standalone solution inventory file configuration for controller container

```
; single node
[contrail-controllers]
10.84.32.10             controller_components=['config','webui']

[contrail-analyticsdb]
10.84.32.10

[contrail-analytics]
10.84.32.10
```

```
; multi node cluster
[contrail-controllers]
10.84.32.10             controller_components=['config','webui']
10.84.32.11             controller_components=['config','webui']
10.84.32.12             controller_components=['config','webui']

[contrail-analyticsdb]
10.84.32.10
10.84.32.11
10.84.32.12

[contrail-analytics]
10.84.32.10
10.84.32.11
10.84.32.12
```

### 9.1.3 analytics standalone solution server.json

```
; single node
{
    "cluster_id": "cluster1",
    "domain": "sm-domain.com",
    "id": "server1",
    "parameters" : {
        "provision": {
            "contrail_4": {
               "controller_components": "['config','webui']"
            },
    …
    …
}

; multi node
{
    "cluster_id": "cluster1",
    "domain": "sm-domain.com",
    "id": "server1",
    "parameters" : {
        "provision": {
            "contrail_4": {
               "controller_components": "['config','webui']"
            },
    …
    …
},
{
    "cluster_id": "cluster1",
    "domain": "sm-domain.com",
    "id": "server2",
    "parameters" : {
        "provision": {
            "contrail_4": {
               "controller_components": "['config','webui']"
            },
    …
    …
},
{
    "cluster_id": "cluster1",
    "domain": "sm-domain.com",
    "id": "server3",
    "parameters" : {
        "provision": {
            "contrail_4": {
               "controller_components": "['config','webui']"
            },
    …
    …
}
```

### 9.1.4 analytics standalone solution sample json config files
### 9.1.4.1 all-in-one node
contrail+openstack on single node
file combined.json.sangupta-u14-working
```
{
    "cluster": [
        {
            "id": "sangupta-u14",
            "parameters": {
                "domain": "contrail.juniper.net",
                "subnet_mask": "255.255.0.0",
                "gateway": "192.168.0.1",
                "provision": {
                    "contrail": {
                        "kernel_upgrade": false
                    },
                    "contrail_4": {
                    },
                    "openstack": {
                        "openstack_manage_amqp": true
                    }
                }
            }
        }
    ],
    "server": [
        {
            "cluster_id": "sangupta-u14",
            "domain": "contrail.juniper.net",
            "id": "sangupta-u14",
            "network": {
                "interfaces": [
                    {
                        "default_gateway": "192.168.0.1",
                        "ip_address": "192.168.0.17/16",
                        "mac_address": "02:bd:08:9a:d8:be",
                        "dhcp": false,
                        "name": "eth0"
                    }
                ],
                "management_interface": "eth0",
                "provisioning": "kickstart"
            },
            "parameters": {
                "partition": "/dev/vda",
                "provision": {
                    "contrail": {
                        "kernel_upgrade": false
                    },
                    "contrail_4": {
                        "controller_components": "['config','webui']"
                    }
                }
            },
            "password": "c0ntrail123",
            "ipmi_address": "",
            "roles": [
                "contrail-analytics",
                "contrail-controller",
                "openstack",
                "contrail-analyticsdb"
            ]
        }
    ],
    "image": [
        {
            "category": "package",
            "id": "image_contrail_cloud_package_4_0_1_0_32",
            "parameters": {
                "contrail-container-package": true
            },
            "path": "/root/sangupta/contrail-cloud-docker_4.0.1.0-32-mitaka_trusty.tgz",
            "type": "contrail-ubuntu-package",
            "version": "4.0.1.0-32"
        }
    ]
}
```
### 9.1.4.2 two node setup - no-auth
a) server-manager+openstack
b) contrail node, aaa-mode=no-auth
file combined.json.sangupta-u14d.contrail
```
{
    "cluster": [
        {
            "id": "sangupta-u14d",
            "parameters": {
                "provision": {
                    "contrail_4": {
                        "api_config": {
                            "aaa_mode": "no-auth"
                        },
                        "analytics_api_config": {
                            "aaa_mode": "no-auth"
                        }
                    },
                    "openstack": {
                        "openstack_manage_amqp": false,
                        "external_openstack_ip": "10.84.35.99"
                    }
                }
            }
        }
    ],
    "server": [
        {
            "cluster_id": "sangupta-u14d",
            "domain": "contrail.juniper.net",
            "id": "sangupta-u14d",
            "network": {
                "interfaces": [
                    {
                        "default_gateway": "192.168.0.1",
                        "ip_address": "192.168.0.137/16",
                        "mac_address": "02:bd:08:9a:d8:be",
                        "dhcp": false,
                        "name": "eth0"
                    }
                ],
                "management_interface": "eth0",
                "provisioning": "kickstart"
            },
            "parameters": {
                "partition": "/dev/vda",
                "provision": {
                    "contrail": {
                        "kernel_upgrade": false
                    },
                    "contrail_4": {
                        "controller_components": "['config','webui']"
                    }
                }
            },
            "password": "c0ntrail123",
            "ipmi_address": "",
            "roles": [
                "contrail-analytics",
                "contrail-controller",
                "contrail-analyticsdb"
            ]
        }
    ],
    "image": [
        {
            "category": "package",
            "id": "image_contrail_networking_package_4_0_1_0_32",
            "parameters": {
                "openstack_sku": "mitaka"
            },
            "path": "/root/sangupta/contrail-networking-docker_4.0.1.0-32_trusty.tgz",
            "type": "contrail-ubuntu-package",
            "version": "4.0.1.0-32"
        }
    ]
}
```

file combined.json.sangupta-u14d2.openstack
```
{
    "cluster": [
        {
            "id": "sangupta-u14d2",
            "parameters": {
                "provision": {
                    "contrail": {
                        "config": {
                            "config_ip_list": ["10.84.35.100"]
                        }
                    },
                    "openstack": {
                        "openstack_manage_amqp": true,
                        "keystone": {
                            "admin_password": "contrail123"
                        }
                    }
                }
            }
        }
    ],
    "server": [
        {
            "cluster_id": "sangupta-u14d2",
            "domain": "contrail.juniper.net",
            "id": "sangupta-u14d2",
            "network": {
                "interfaces": [
                    {
                        "default_gateway": "192.168.0.1",
                        "ip_address": "192.168.0.143/16",
                        "mac_address": "02:bd:08:9a:d8:bd",
                        "dhcp": false,
                        "name": "eth0"
                    }
                ],
                "management_interface": "eth0",
                "provisioning": "kickstart"
            },
            "password": "c0ntrail123",
            "ipmi_address": "",
            "roles": [
                "openstack"
            ]
        }
    ],
    "image": [
        {
            "category": "package",
            "id": "image_contrail_cloud_package_4_0_1_0_32",
            "path": "/root/sangupta/contrail-cloud-docker_4.0.1.0-32-mitaka_trusty.tgz",
            "type": "contrail-ubuntu-package",
            "version": "4.0.1.0-32"
        }
    ]
}
```
### 9.1.4.3 external openstack/keystone
a) server-manager+openstack
b) contrail node
file combined.json.sangupta-u14d.contrail
```
{
    "cluster": [
        {
            "id": "sangupta-u14d",
            "parameters": {
                "provision": {
                    "contrail_4": {
                        "keystone_config": {
                            "ip": "10.84.35.99",
                            "admin_password": "contrail123",
                            "admin_tenant": "admin"
                        }
                    },
                    "openstack": {
                        "openstack_manage_amqp": false,
                        "external_openstack_ip": "10.84.35.99"
                    }
                }
            }
        }
    ],
    "server": [
        {
            "cluster_id": "sangupta-u14d",
            "domain": "contrail.juniper.net",
            "id": "sangupta-u14d",
            "network": {
                "interfaces": [
                    {
                        "default_gateway": "192.168.0.1",
                        "ip_address": "192.168.0.137/16",
                        "mac_address": "02:bd:08:9a:d8:be",
                        "dhcp": false,
                        "name": "eth0"
                    }
                ],
                "management_interface": "eth0",
                "provisioning": "kickstart"
            },
            "parameters": {
                "partition": "/dev/vda",
                "provision": {
                    "contrail": {
                        "kernel_upgrade": false
                    },
                    "contrail_4": {
                        "controller_components": "['config','webui']"
                    }
                }
            },
            "password": "c0ntrail123",
            "ipmi_address": "",
            "roles": [
                "contrail-analytics",
                "contrail-controller",
                "contrail-analyticsdb"
            ]
        }
    ],
    "image": [
        {
            "category": "package",
            "id": "image_contrail_networking_package_4_0_1_0_32",
            "parameters": {
                "openstack_sku": "mitaka"
            },
            "path": "/root/sangupta/contrail-networking-docker_4.0.1.0-32_trusty.tgz",
            "type": "contrail-ubuntu-package",
            "version": "4.0.1.0-32"
        }
    ]
}
```

file combined.json.sangupta-u14d2.openstack
```
{
    "cluster": [
        {
            "id": "sangupta-u14d2",
            "parameters": {
                "provision": {
                    "contrail": {
                        "config": {
                            "config_ip_list": ["10.84.35.100"]
                        }
                    },
                    "openstack": {
                        "openstack_manage_amqp": true,
                        "keystone": {
                            "admin_password": "contrail123"
                        }
                    }
                }
            }
        }
    ],
    "server": [
        {
            "cluster_id": "sangupta-u14d2",
            "domain": "contrail.juniper.net",
            "id": "sangupta-u14d2",
            "network": {
                "interfaces": [
                    {
                        "default_gateway": "192.168.0.1",
                        "ip_address": "192.168.0.143/16",
                        "mac_address": "02:bd:08:9a:d8:bd",
                        "dhcp": false,
                        "name": "eth0"
                    }
                ],
                "management_interface": "eth0",
                "provisioning": "kickstart"
            },
            "password": "c0ntrail123",
            "ipmi_address": "",
            "roles": [
                "openstack"
            ]
        }
    ],
    "image": [
        {
            "category": "package",
            "id": "image_contrail_cloud_package_4_0_1_0_32",
            "path": "/root/sangupta/contrail-cloud-docker_4.0.1.0-32-mitaka_trusty.tgz",
            "type": "contrail-ubuntu-package",
            "version": "4.0.1.0-32"
        }
    ]
}
```

## 9.2 Dev tests
## 9.3 System tests

# 10. Documentation Impact

# 11. References


