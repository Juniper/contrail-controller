# vim: tabstop=4 shiftwidth=4 softtabstop=4
#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#

"""
CNI implementation
Demultiplexes on the CNI_COMMAND and runs the necessary operation
"""
import argparse
import inspect
import json
import logging
import os
import sys


# set parent directory in sys.path
cfile = os.path.abspath(inspect.getfile(inspect.currentframe()))  # nopep8
sys.path.append(os.path.dirname(os.path.dirname(cfile)))  # nopep8
from common.cni import Cni as Cni
from common.veth import CniVEthPair as CniVEthPair
from common.macvlan import CniMacVlan as CniMacVlan
from contrail.vrouter import VRouter as VRouter
from contrail.vrouter import Error as Error


# Error codes
CONTRAIL_CNI_UNSUPPORTED_CMD = 501


# logger for the file
logger = None


class ContrailCni():
    # Additional CNI commands supported by Contrail. Used in debugging and
    # developement cycles
    CONTRAIL_CNI_CMD_GET = 'get'
    CONTRAIL_CNI_CMD_POLL = 'poll'

    # Container orchestrator modes
    CONTRAIL_CNI_MODE_K8S = "k8s"
    CONTRAIL_CNI_MODE_MESOS = "mesos"

    # Type of virtual interface to be created for container
    CONTRAIL_VIF_TYPE_VETH = "veth"
    CONTRAIL_VIF_TYPE_MACVLAN = "macvlan"

    # In case of macvlan, the container interfaces will run as sub-interface
    # to interface on host network-namespace. Name of the interface inside
    # host network-namespace is defined below
    CONTRAIL_PARENT_INTERFACE = "eth0"

    # Logging parameters
    CONTRAIL_LOG_FILE = '/var/log/contrail/cni/opencontrail.log'
    CONTRAIL_LOG_LEVEL = 'WARNING'

    def __init__(self):
        # set logging
        self.log_file = ContrailCni.CONTRAIL_LOG_FILE
        self.log_level = ContrailCni.CONTRAIL_LOG_LEVEL
        self.mode = ContrailCni.CONTRAIL_CNI_MODE_K8S
        self.vif_type = ContrailCni.CONTRAIL_VIF_TYPE_VETH
        self.parent_interface = ContrailCni.CONTRAIL_PARENT_INTERFACE
        self.conf_file = None
        self.stdin_string = None
        self.args_uuid = None

        # Read CLI arguments
        self._get_params_from_cli()
        # Get contrail specific parameters
        self._get_params()

        # Get logging parameters and configure logging
        self._configure_logging()
        global logger
        logger = logging.getLogger('contrail-cni')

        self.vrouter = VRouter(self.stdin_string)
        self.cni = Cni(self.stdin_string)
        self.cni.update(self.args_uuid, None)
        return

    # Read parameters passed as cli-arguments
    def _get_params_from_cli(self):
        parser = argparse.ArgumentParser(description='CNI Arguments')
        parser.add_argument('-c', '--command',
                            help='CNI command add/del/version/get/poll')
        parser.add_argument('-v', '--version', action='version', version='0.1')
        parser.add_argument('-f', '--file', help='Contrail CNI config file')
        parser.add_argument('-u', '--uuid', help='Container UUID')
        args = parser.parse_args()

        # Override CNI_COMMAND environment
        if args.command is not None:
            os.environ['CNI_COMMAND'] = args.command

        # Set UUID from argument. If valid-uuid is found, it will overwritten
        # later. Useful in case of UT where valid uuid for pod cannot be found
        self.args_uuid = args.uuid
        self.conf_file = args.file
        return

    @staticmethod
    def parse_mode(mode):
        if mode.lower() == ContrailCni.CONTRAIL_CNI_MODE_K8S:
            return ContrailCni.CONTRAIL_CNI_MODE_K8S
        if mode.lower() == ContrailCni.CONTRAIL_CNI_MODE_MESOS:
            return ContrailCni.CONTRAIL_CNI_MODE_MESOS
        return ContrailCni.CONTRAIL_CNI_MODE_K8S

    @staticmethod
    def parse_vif_type(vif_type):
        if vif_type.lower() == ContrailCni.CONTRAIL_VIF_TYPE_VETH:
            return ContrailCni.CONTRAIL_VIF_TYPE_VETH
        if vif_type.lower() == ContrailCni.CONTRAIL_VIF_TYPE_MACVLAN:
            return ContrailCni.CONTRAIL_VIF_TYPE_MACVLAN
        return ContrailCni.CONTRAIL_VIF_TYPE_VETH

    def _get_params(self):
        # Read config file from STDIN or optionally from a file
        if self.conf_file:
            with open(self.conf_file, 'r') as f:
                self.stdin_string = f.read()
        else:
            self.stdin_string = sys.stdin.read()
        self.stdin_json = json.loads(self.stdin_string)

        contrail_json = self.stdin_json.get('contrail')
        if contrail_json is None:
            return


        if contrail_json.get('log-file') is not None:
            self.log_file = contrail_json['log-file']
        if contrail_json.get('log-level') is not None:
            self.log_level = contrail_json['log-level']
        if contrail_json.get('mode') != None:
            self.mode = self.parse_mode(contrail_json['mode'])
        if contrail_json.get('vif-type') != None:
            self.vif_type = self.parse_vif_type(contrail_json['vif-type'])
        if contrail_json.get('parent-interface') != None:
            self.parent_interface = contrail_json['parent-interface']
        return

    def _configure_logging(self):
        # Configure logger
        time_format = '%(asctime)s:%(name)s:%(levelname)s:%(message)s '
        date_format = '%m/%d/%Y %I:%M:%S %p '
        logging.basicConfig(filename=self.log_file,
                            level=self.log_level.upper(),
                            format=time_format, datefmt=date_format)
        return

    def build_response(self, vr_resp):
        self.cni.build_response(vr_resp['ip-address'], vr_resp['plen'],
                                vr_resp['gateway'], vr_resp['dns-server'])
        return

    def get_cmd(self):
        resp = self.vrouter.get_cmd(self.cni.container_uuid,
                                    self.cni.container_vn)
        return self.build_response(resp)

    def poll_cmd(self):
        resp = self.vrouter.poll_cmd(self.cni.container_uuid,
                                     self.cni.container_vn)
        return self.build_response(resp)

    def _make_interface(self, mac, vlan_tag):
        # Create the interface object
        intf = None
        if self.vif_type == ContrailCni.CONTRAIL_VIF_TYPE_MACVLAN:
            host_ifname = self.parent_interface
            intf = CniMacVlan(self.cni, mac, host_ifname, vlan_tag)
        else:
            intf = CniVEthPair(self.cni, mac)
            host_ifname = intf.host_ifname
        return intf, host_ifname

    def add_cmd(self):
        '''
        ADD handler for a container
        - Pre-fetch interface configuration from VRouter.
          - Gets MAC address for the interface
          - In case of sub-interface, gets VLAN-Tag for the interface
        - Create interface based on the "mode"
        - Invoke Add handler from VRouter module
        - Update interface with configuration got from VRouter
          - Configures IP address
          - Configures routes
          - Bring-up the interface
        - stdout CNI response
        '''
        # Pre-fetch initial configuration for the interface from vrouter
        # This will give MAC address for the interface and in case of
        # VMI sub-interface, we will also get the vlan-tag
        cfg = self.vrouter.poll_cfg_cmd(self.cni.container_uuid,
                                        self.cni.container_vn)

        # Create the interface object
        intf, host_ifname = self._make_interface(cfg.get('mac-address'),
                                                 cfg.get('vlan-id'))
        # Create the interface both in host-os and inside container
        intf.create_interface()

        # Inform vrouter about interface-add. The interface inside container
        # must be created by this time
        resp = self.vrouter.add_cmd(self.cni.container_uuid,
                                    self.cni.container_id,
                                    self.cni.container_name,
                                    None, host_ifname,
                                    self.cni.container_ifname,
                                    self.cni.container_vn)
        # Configure the interface based on config received above
        intf.configure_interface(resp['ip-address'], resp['plen'],
                                 resp['gateway'])

        # Build CNI response and print on stdout
        return self.build_response(resp)

    def delete_cmd(self):
        '''
        DEL handler for a container
        - Delete veth pair
        - Invoke Delete handler from VRouter module
        - stdout VRouter response
        '''
        # Create the interface object
        intf, host_ifname = self._make_interface('00:00:00:00:00:00', None)

        # Delete the interface
        intf.delete_interface()

        # Inform VRouter about interface delete
        self.vrouter.delete_cmd(self.cni.container_uuid,
                                self.cni.container_vn)

        self.cni.delete_response()
        return

    def Version(self):
        '''
        Return Version
        '''
        self.cni.version_response()
        return

    def Run(self):
        '''
        main method for CNI plugin
        '''
        if self.cni.command.lower() == Cni.CNI_CMD_VERSION:
            resp = self.Version()
        elif self.cni.command.lower() == Cni.CNI_CMD_ADD:
            resp = self.add_cmd()
        elif (self.cni.command.lower() == Cni.CNI_CMD_DELETE or
              self.cni.command.lower() == Cni.CNI_CMD_DEL):
            resp = self.delete_cmd()
        elif self.cni.command.lower() == ContrailCni.CONTRAIL_CNI_CMD_GET:
            resp = self.get_cmd()
        elif self.cni.command.lower() == ContrailCni.CONTRAIL_CNI_CMD_POLL:
            resp = self.poll_cmd()
        else:
            raise Error(CONTRAIL_CNI_UNSUPPORTED_CMD,
                        'Invalid command ' + self.cni.command)
        return

    def log(self):
        logger.debug('mode = ' + self.mode + ' vif-type = ' + self.vif_type +
                     ' parent-interface = ' + self.parent_interface)
        self.cni.log()
        self.vrouter.log()
        return
