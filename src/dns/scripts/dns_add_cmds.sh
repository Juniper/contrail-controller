#
#Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

python add_virtual_dns.py --api_server_ip 10.204.216.39 --api_server_port 8082 --name con_vdns --domain_name default-domain --dns_domain contrail.com --dyn_updates --record_order random --ttl 20001  --next_vdns vdns1

python add_virtual_dns_record.py --api_server_ip 10.204.216.39 --api_server_port 8082 --name con_rec --vdns_fqname default-domain:con_vdns --rec_name two --rec_type A --rec_class IN --rec_data 1.2.3.14 --rec_ttl 44552

python associate_virtual_dns.py --api_server_ip 10.204.216.39 --api_server_port 8082 --ipam_fqname default-domain:demo:con_ipam --ipam_dns_method default-dns-server

python associate_virtual_dns.py --api_server_ip 10.204.216.39 --api_server_port 8082 --ipam_fqname default-domain:demo:con_ipam --ipam_dns_method virtual-dns-server --vdns_fqname default-domain:con_vdns

