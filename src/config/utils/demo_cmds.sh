#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#
python demo_cfg.py --api_server_ip 10.1.10.2 --api_server_port 8082 --public_subnet 1.1.1.0/24
python provision_mx.py --api_server_ip 10.1.10.2 --api_server_port 8082 --router_name mx1 --router_ip 10.1.10.100 --router_asn 64512
python provision_mx.py --api_server_ip 10.1.10.2 --api_server_port 8082 --router_name mx2 --router_ip 10.1.10.101 --router_asn 64512
python provision_control.py --api_server_ip 10.1.10.2 --api_server_port 8082 --host_name a1s1 --host_ip 10.1.10.3 --router_asn 64512
python provision_control.py --api_server_ip 10.1.10.2 --api_server_port 8082 --host_name a1s2 --host_ip 10.1.10.4 --router_asn 64512
python add_route_target.py --routing_instance_name default-domain:demo:public:public --route_target_number 10000 --router_asn 64512 --api_server_ip 10.1.10.2 --api_server_port 8082
python create_floating_pool.py --public_vn_name default-domain:demo:public --floating_ip_pool_name pub_fip_pool --api_server_ip 10.1.10.2 --api_server_port 8082
python use_floating_pool.py --project_name default-domain:demo --floating_ip_pool_name default-domain:demo:public:pub_fip_pool --api_server_ip 10.1.10.2 --api_server_port 8082
