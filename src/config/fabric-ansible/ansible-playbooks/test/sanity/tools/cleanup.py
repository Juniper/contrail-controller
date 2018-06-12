from vnc_api.vnc_api import VncApi
from vnc_api.gen.resource_client import (
    Fabric,
    FabricNamespace,
    VirtualNetwork,
    NetworkIpam
)

    
vnc_api = VncApi()

#import pdb; pdb.set_trace()
namespaces = vnc_api.fabric_namespaces_list(detail=True)
for ns in namespaces:
    vnc_api.fabric_namespace_delete(id=ns.uuid)

fabs = vnc_api.fabrics_list(detail=True)
for fab in fabs:
    # remove fabric->vn refs
    fab.set_virtual_network_list([])
    vnc_api.fabric_update(fab)
    
    # remove fabric->node_profile refs     
    fab.set_node_profile_list([])
    vnc_api.fabric_update(fab)
   
    # remove fabric
    vnc_api.fabric_delete(id=fab.uuid)

role_configs = vnc_api.role_configs_list(detail=True)
for role_config in role_configs:
    vnc_api.role_config_delete(id=role_config.uuid)

node_profiles = vnc_api.node_profiles_list(detail=True)
for node_profile in node_profiles:
    node_profile.set_hardware_list([])
    vnc_api.node_profile_update(node_profile)
    vnc_api.node_profile_delete(id=node_profile.uuid)

job_templates = vnc_api.job_templates_list(detail=True)
for job_template in job_templates:
    vnc_api.job_template_delete(id=job_template.uuid)
