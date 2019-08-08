#!/usr/bin/python

from __future__ import print_function
from builtins import str
from builtins import object
from future import standard_library
standard_library.install_aliases()  # noqa

from ironicclient import client as ironicclient
from urllib.parse import urlparse
from keystoneauth1.identity import v3
from keystoneauth1 import session
from keystoneclient.v3 import client
import ironic_inspector_client
from contrail_command import CreateCCNode
import time

class ImportIronicNodes(object):

    added_nodes_list = None
    auth_token = None
    auth_url = None
    ironic_client = None
    ironic_inspector_client = None
    keystone_auth = None
    keystone_session = None
    keystone_client = None
    cc_node = None
    introspection_timeout_secs = 1200
    introspection_check_secs = 30
    node_info_list = []
    uuid_node_map = {}

    # Ironic Default Auth Args
    auth_args = {
        'auth_url': None,
        'auth_protocol': 'http',
        'auth_host': "",
        'username': "admin",
        'password': "",
        'auth_port': '5000',
        'user_domain_name': 'default',
        'project_domain_name': 'default',
        'project_name': 'admin',
        'auth_url_version': "/v2.0",
        'aaa_mode': None
    }

    ironic_client_kwargs = {
        'os_auth_url': '',
        'os_username': '',
        'os_password': '',
        'os_project_name': 'admin',
        'os_user_domain_name': 'default',
        'os_project_domain_name': 'default',
        'os_endpoint_type': 'internalURL',
        'os_ironic_api_version': "1.19"
    }

    auth_headers = {
        'Content-Type': 'application/json'
    }

    def __init__(self, auth_args, cluster_id=None, cluster_token=None,
                 cc_host=None, cc_username=None,cc_password=None,
                 added_nodes_list=None):
        for key,val in list(auth_args.items()):
            if key in self.auth_args:
                self.auth_args[key] = val
            elif key in self.ironic_client_kwargs:
                self.ironic_client_kwargs[key] = val
            if 'os_' + str(key) in self.ironic_client_kwargs:
                self.ironic_client_kwargs['os_'+str(key)] = val

        if not self.auth_args['auth_url']:
            self.auth_url = '%s://%s:%s%s' % (self.auth_args['auth_protocol'],
                                              self.auth_args['auth_host'],
                                              self.auth_args['auth_port'],
                                              self.auth_args[
                                                  'auth_url_version'])
        else:
            self.auth_url = self.auth_args['auth_url']
            parsed_auth_url = urlparse(self.auth_url)
            self.auth_args['auth_protocol'] = parsed_auth_url.scheme
            self.auth_args['auth_host'] = parsed_auth_url.hostname
            self.auth_args['auth_port'] = parsed_auth_url.port
            self.auth_args['auth_url_version'] = parsed_auth_url.path

        if added_nodes_list:
            self.added_nodes_dict = {node['uuid']: node for node in
                                     added_nodes_list}

        # Set authentication and client auth objects
        self.set_ks_auth_sess()
        self.set_ironic_clients()
        self.auth_token = self.keystone_session.get_token()
        self.auth_headers['X-Auth-Token'] = self.auth_token
        self.cc_node = CreateCCNode(cc_host, cluster_id, cluster_token,
                                    cc_username, cc_password)

    def set_ks_auth_sess(self):
        self.keystone_auth = v3.Password(
            auth_url=self.auth_url,
            username=self.auth_args['username'],
            password=self.auth_args['password'],
            project_name=self.auth_args['project_name'],
            user_domain_name=self.auth_args['user_domain_name'],
            project_domain_name=self.auth_args['project_domain_name']
        )
        self.keystone_session = session.Session(
            auth=self.keystone_auth,
            verify=False)
        self.keystone_client = client.Client(
            session=self.keystone_session)

    def set_ironic_clients(self):
        self.ironic_client_kwargs['os_auth_url'] = self.auth_url
        self.ironic_client = \
            ironicclient.get_client(1, **self.ironic_client_kwargs)
        self.ironic_inspector_client = ironic_inspector_client.ClientV1(
            session=self.keystone_session)

    def read_nodes_from_db(self):
        all_ironic_nodes = self.ironic_client.node.list(detail=True)

        for node_dict in all_ironic_nodes:
            node_dict = node_dict.to_dict()

            if self.added_nodes_dict:
                if node_dict.get('uuid', None) in list(self.added_nodes_dict.keys()):
                    self.node_info_list.append(node_dict)
                    self.uuid_node_map[str(node_dict['uuid'])] = node_dict
            else:
                self.node_info_list.append(node_dict)
                self.uuid_node_map[str(node_dict['uuid'])] = node_dict

    def check_introspection(self):
        all_introspected_nodes = self.ironic_inspector_client.list_statuses()
        nodes_to_check_introspected = []

        uuid_list = []
        failed_nodes = []
        success_nodes = []

        for node in self.node_info_list:
            uuid_list.append(node['uuid'])

        for node in all_introspected_nodes:
            if node['uuid'] in uuid_list:
                nodes_to_check_introspected.append(
                    self.uuid_node_map[node['uuid']]
                )

        if not len(nodes_to_check_introspected):
            return [], []
        else:
            success_nodes, failed_nodes = self.wait_for_introspection(
                nodes_to_check_introspected
            )
        return success_nodes, failed_nodes

    def wait_for_introspection(self, node_list):

        timeout = 0
        failed_nodes = []
        success_nodes = []
        while len(node_list) and timeout < self.introspection_timeout_secs:
            new_node_list = []
            for node in node_list:
                intro_status = self.check_introspection_status(node)
                if intro_status == "finished":
                    print("CREATING CC NODE", node)
                    # Get latest Node Info from Ironic POST Introspection
                    node_info = self.ironic_client.node.get(node['uuid'])
                    new_node_info = node_info.to_dict()

                    # Merge with known IPMI Info if available
                    if self.added_nodes_dict and node['uuid'] in \
                            list(self.added_nodes_dict.keys()):
                        node_ipmi_info = self.added_nodes_dict[node['uuid']]
                        new_node_info['driver_info'] = {
                            str("ipmi_" + k): node_ipmi_info[k] for k in [
                                'username', 'password', 'address', 'port']
                        }

                    self.create_cc_node(new_node_info)
                    success_nodes.append(new_node_info['uuid'])
                elif intro_status == "error":
                    self.log_introspection_error([node])
                    failed_nodes.append(node['uuid'])
                else:
                    new_node_list.append(node)

            node_list = new_node_list
            if len(node_list):
                print("SLEEPING FOR NEXT INSPECT", timeout)
                time.sleep(self.introspection_check_secs)
                timeout += self.introspection_check_secs

        if len(node_list):
            self.log_introspection_error(node_list)
            for node in node_list:
                failed_nodes.append(node['uuid'])
        return success_nodes, failed_nodes

    def check_introspection_status(self, node):
        status = self.ironic_inspector_client.get_status(node['uuid'])
        if status['finished']:
            return "finished"
        elif status['error']:
            return "error"
        else:
            return "running"

    def log_introspection_error(self, node_list):
        print("LOG INTROSPECTION ERROR")
        for node in node_list:
            print("FAILED NODES", node['uuid'])
        return

    def get_cc_port_payload(self, port_dict, local_link_dict):
        cc_port = {"kind": "port",
            "data": {
                "parent_type": "node",
                "parent_uuid": port_dict['node_uuid'],
                "name": port_dict['ifname'],
                "uuid": port_dict['uuid'],
                "bms_port_info": {
                    "pxe_enabled": port_dict['pxe_enabled'],
                    "address": port_dict['address'],
                    "node_uuid": port_dict['node_uuid'],
                    "local_link_connection": local_link_dict
                }
            }
        }
        return cc_port

    def get_cc_node_payload(self, node_dict):
        cc_node = {"resources":
            [{
                "kind": "node",
                "data": {
                    "uuid": node_dict['uuid'],
                    "node_type": "baremetal",
                    "name": node_dict['hostname'],
                    "display_name": node_dict['hostname'],
                    "hostname": node_dict['hostname'],
                    "parent_type": "global-system-config",
                    "fq_name": ["default-global-system-config",
                                node_dict['uuid']],
                    "bms_info": {
                        "name": node_dict['hostname'],
                        "network_interface": "neutron",
                        "properties": node_dict['properties'],
                        "driver": "pxe_ipmitool",
                        "driver_info": node_dict["driver_info"]
                    }
                }
            }]
        }
        cc_node['resources'][0]['data']['bms_info']['driver_info']['ipmi_port'] = str(cc_node['resources'][0]['data']['bms_info']['driver_info']['ipmi_port'])

        return cc_node

    def get_processed_if_data(self, introspection_data):
        processed_data = {}
        interface_data_dict = introspection_data['all_interfaces']

        for if_name,if_data in list(interface_data_dict.items()):
            mac_address = if_data['mac']
            processed_data[mac_address] = {'ifname': if_name}
            if 'lldp_processed' in if_data:
                processed_data[mac_address]['switch_name'] = \
                    if_data['lldp_processed']['switch_system_name']
                processed_data[mac_address]['port_name'] = \
                    if_data['lldp_processed']['switch_port_description']
        return processed_data

    def convert_port_format(self, port_list, introspection_data, uuid):
        cc_port_list = []
        generated_hostname = ""
        processed_interface_dict = self.get_processed_if_data(
         introspection_data)

        for port in port_list:
            port_dict = port.to_dict()
            local_link_dict = port_dict['local_link_connection']
            mac = port_dict['address']
            ifname = "p-" + mac.replace(":", "")[6:]
            port_dict['ifname'] = ifname
            if port_dict['pxe_enabled']:
                generated_hostname = "auto-" + mac.replace(":", "")[6:]
            if mac in processed_interface_dict:
                local_link_dict['switch_info'] = \
                    processed_interface_dict[mac].get('switch_name',"")
                local_link_dict['port_id'] = \
                    processed_interface_dict[mac].get('port_name', "")

            cc_port = self.get_cc_port_payload(port_dict, local_link_dict)
            cc_port_list.append(cc_port)
        return generated_hostname, cc_port_list

    def create_cc_node(self, node_dict):
        port_list = self.ironic_client.port.list(
            node=node_dict['uuid'], detail=True)
        introspection_data = self.ironic_inspector_client.get_data(
            node_dict['uuid'])
        hostname, port_list = self.convert_port_format(port_list,
                                                       introspection_data,
                                                       node_dict['uuid'])
        node_dict['hostname'] = hostname
        for k in ['cpus', 'local_gb', 'memory_mb']:
            node_dict['properties'][k] = int(node_dict['properties'][k])
        cc_node_payload = self.get_cc_node_payload(node_dict)
        cc_node_payload['resources'].extend(port_list)

        self.cc_node.create_cc_node(cc_node_payload)

    def register_nodes(self, node_ipmi_details):
        registered_nodes_list = []
        for node in node_ipmi_details:
            ironic_node = {}
            ironic_node['driver'] = 'pxe_ipmitool'
            ironic_node['driver_info'] = {
                str("ipmi_" + k): v for (k, v) in list(node.items())
            }
            try:
                resp = self.ironic_client.node.create(**ironic_node)
                print(resp.uuid)
                node['uuid'] = resp.uuid
                registered_nodes_list.append(node)
            except Exception as error:
                print("ERROR: ", error)
                raise error

        return registered_nodes_list

    def trigger_introspection(self, registered_nodes):
        for node in registered_nodes:
            print(node['uuid'])
            try:
                self.ironic_inspector_client.introspect(node['uuid'])
                node['inspect'] = True
            except Exception as error:
                node['inspect'] = False
                print("ERROR: ", error)
                raise error
        return registered_nodes


def main(added_nodes_list=None, ironic_auth_args=None, cc_auth_host=None,
         cc_auth_token=None, introspection_flag = False):
    import_ironic_nodes_obj = ImportIronicNodes(auth_args=ironic_auth_args,
                                                cc_host=cc_auth_host,
                                                cc_auth_token=cc_auth_token,
                                                added_nodes_list=
                                                added_nodes_list)
    import_ironic_nodes_obj.read_nodes_from_db()
    if introspection_flag:
        import_ironic_nodes_obj.check_introspection()
    else:
        for node in import_ironic_nodes_obj.node_info_list:
            import_ironic_nodes_obj.create_cc_node(node)


if __name__ == '__main__':
    my_auth_args = {
        'auth_url': 'http://1.1.1.1:5000/v3',
        'username': "admin",
        'password': "admin",
        'user_domain_name': 'default',
        'project_domain_name': 'default',
        'project_name': 'admin',
        'aaa_mode': None
    }
    introspection_flag = False
    cc_auth_host = "10.10.10.10"
    try:
        main(ironic_auth_args=my_auth_args,
             cc_auth_host=cc_auth_host,
             cc_auth_token=None,
             introspection_flag=introspection_flag)
    except Exception as e:
        print(e.message)

