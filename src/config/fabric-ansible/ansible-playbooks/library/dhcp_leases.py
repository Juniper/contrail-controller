from ansible.module_utils.basic import *
#from ansible.module_utils.fabric_utils import FabricAnsibleModule
import logging
import json

def process_mac_list(module):
    import re

    mac_list_in = module.params['mac_list']
    mac_list = mac_list_in['mac_list']
    pattern = re.compile(r"lease ([0-9.]+) {.*?hardware ethernet ([:a-f0-9]+);.*?}", re.MULTILINE | re.DOTALL)

    lease_table = {}

    with open("/var/lib/dhcpd/dhcpd.leases") as f:
        for match in pattern.finditer(f.read()):
             lease_table[match.group(2)] = match.group(1)
    #module.logger.error("MAC_LIST".format(mac_list))
    logging.error("TESTING123")

    results = "ok"

    for mac in mac_list:
        if mac not in lease_table:
            results = mac
            break
  
    return {"results":results}

def main():
    module = AnsibleModule(
        argument_spec=dict(
            mac_list=dict(type='dict', required=True)
            ),
        supports_check_mode=False)

    results = process_mac_list(module)
    module.exit_json(**results)

if __name__ == '__main__':
    main()
