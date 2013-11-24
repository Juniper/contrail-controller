# Before running this script do "source /opt/contrail/api-venv/bin/activate"
# Usage: python encap_set.py <add|update|delete> <username> <password> <tenant_name> <config_node_ip>
from vnc_api.vnc_api import *

if len(sys.argv) < 6:
    print 'Usage: python encap_set.py <add|update|delete> <username> <password> <tenant_name> <config_node_ip>'
    sys.exit(0)

if __name__ == "__main__":
    handle= VncApi(username=sys.argv[2], password= sys.argv[3], tenant_name=sys.argv[4], api_server_host= sys.argv[5], api_server_port= '8082')
    encap_obj=EncapsulationPrioritiesType(encapsulation=['MPLSoGRE','MPLSoUDP','VXLAN'])
    conf_obj=GlobalVrouterConfig(encapsulation_priorities=encap_obj, evpn_status='true')
    #conf_obj=GlobalVrouterConfig(encapsulation_priorities=encap_obj,vxlan_network_identifier_mode='automatic')
    if sys.argv[1] == "add":
        result=handle.global_vrouter_config_create(conf_obj)
        print 'Created.UUID is %s'%(result)
    elif sys.argv[1] == "update":
        result=handle.global_vrouter_config_update(conf_obj)
        print 'Updated.%s'%(result)
    elif sys.argv[1] == "delete":
        conf_id=handle.get_default_global_vrouter_config_id()
        handle.global_vrouter_config_delete(id=conf_id)
    else:    
        print 'Usage: python encap_set.py <add|update|delete> <username> <password> <tenant_name> <config_node_ip>'

