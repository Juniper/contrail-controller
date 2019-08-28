from __future__ import absolute_import
from builtins import next
from builtins import str
from builtins import object
import os
import time
import sys
import argparse
import subprocess
import uuid

import netaddr
import requests
import json
import paramiko
import socket

from .common import validate_uuid


def call_powershell(cmds):
    powershell_command = subprocess.list2cmdline(cmds)
    process_cmdline = ["powershell.exe", "-NonInteractive", "-Command"]
    process_cmdline.append(powershell_command)
    output = subprocess.check_output(process_cmdline, shell=True)
    return output


LEFT_DEV_PREFIX = 'int'
RIGHT_DEV_PREFIX = 'gw'
WINGW_PREFIX = 'contrail-wingw-'


class BlockingSSHClient(paramiko.SSHClient):

    def __init__(self):
        paramiko.SSHClient.__init__(self)

    def exec_command(self, cmd):
        _, stdout, _ = \
            super(BlockingSSHClient, self).exec_command(cmd)
        return stdout.channel.recv_exit_status()


class VMPort(object):
    NAME_LEN = 14
    DEV_PREFIX = 'eth'
    BASE_URL = "http://localhost:9091/port"
    HEADERS = {'content-type': 'application/json'}

    def __init__(self, uuid, prefix, mac=None, ip_cidr=None, gw_cidr=None):
        self.uuid = validate_uuid(uuid)
        if mac:
            self.mac = netaddr.EUI(mac, dialect=netaddr.mac_eui48)
        if ip_cidr:
            self.ip = netaddr.IPNetwork(ip_cidr)
        if gw_cidr:
            self.gw_cidr = gw_cidr

        if prefix == LEFT_DEV_PREFIX:
            dev_suffix = "1"    # as seen from inside the SNAT VM
            win_prefix = "int"  # as seen from the host
        elif prefix == RIGHT_DEV_PREFIX:
            dev_suffix = "2"
            win_prefix = "gw"
        else:
            raise RuntimeError("prefix must be {} or {}"
                               .format(LEFT_DEV_PREFIX, RIGHT_DEV_PREFIX))
        self.name = self.DEV_PREFIX + dev_suffix
        self.win_name = "{}-{}".format(win_prefix, self.uuid)[:self.NAME_LEN]

    def register(self):
        """registers wingw interface (as seen on host) to agent"""
        # TODO do we have to register mgmt interface?
        # get 2 snat ifaces of wingw VM
        # _add_port_to_agent on those nics
        pass

    def unregister(self):
        """unregisters wingw interfaces (as seen on host) from agent"""
        # TODO do we have to unregister mgmt interface?
        # self._delete_port_from_agent(self.nic_left)
        # self._delete_port_from_agent(self.nic_right)
        pass

    def _request_to_agent(self, url, method, data):
        method = getattr(requests, method)
        resp = method(url, data=data, headers=self.HEADERS)
        if resp.status_code != requests.codes.ok:
            error_str = resp.text
            try:
                err = json.loads(resp.text)
                error_str = err['error']
            except Exception:
                pass
            raise ValueError(error_str)

    def _add_port_to_agent(self, nic, display_name=None):
        # TODO no agent - uncomment and fix when it works
        port_type_value = 1 # NameSpacePort
        payload = {"ip-address": str(nic['ip'].ip), "tx-vlan-id": -1,
                   "display-name": display_name, "id": nic['uuid'],
                   "instance-id": self.vm_uuid, "ip6-address": '',
                   "rx-vlan-id": -1,
                   "system-name": self._get_wingw_iface_name(nic['uuid']),
                   "vn-id": '', "vm-project-id": '',
                   "type": port_type_value, "mac-address": str(nic['mac'])}
        json_dump = json.dumps(payload)
        #self._request_to_agent(self.BASE_URL, 'post', json_dump)

    def _delete_port_from_agent(self, nic):
        # TODO no agent - uncomment and fix when it works
        # self._request_to_agent(url, 'delete', None)
        url = self.BASE_URL + "/" + nic['uuid']

    def _get_wingw_iface_name(self, uuid_str):
        # TODO no agent - uncomment and fix when it works
        #return (self.TAP_PREFIX + uuid_str)[:self.DEV_NAME_LEN]
        pass


class MgmtIPAM(object):
    MGMT_IP_RANGE = netaddr.IPRange("169.254.150.10", "169.254.150.254")

    def __init__(self, mgmt_vswitch_name):
        self.mgmt_vswitch_name = mgmt_vswitch_name

    def generate(self):
        """generates an IP to give to new gateway VM"""
        used_ips = self._get_all_mgmt_ips()

        used_ips = netaddr.IPSet(used_ips)
        pool_ips = netaddr.IPSet(self.MGMT_IP_RANGE)
        free_ips = pool_ips ^ used_ips

        try:
            first_available_ip = next(iter(free_ips))
        except StopIteration:
            raise ValueError("Ran out of management IPs for gateway VM")
        return first_available_ip

    def _get_all_mgmt_ips(self):
        """queries hyper-v for management IPs of all SNAT VMs"""
        try:
            mgmt_ips = self._get_mgmt_ips_of(WINGW_PREFIX + "*")
        except IndexError:
            # ignore, just return an empty list
            mgmt_ips = []
        return mgmt_ips

    def _get_mgmt_ips_of(self, vmname_with_wildcard):
        ips = call_powershell(["Get-VMNetworkAdapter",
                               "-VMName", "{}".format(vmname_with_wildcard),
                               "|", "Where", "SwitchName", "-eq",
                               self.mgmt_vswitch_name, "|", "Select",
                               "-ExpandProperty", "IPAddresses"])
        if ips == "":
            raise IndexError("no management IP found")
        ips = ips.splitlines()

        # if multiple IPs connected to mgmt switch, we can use either one
        return ips[0]


class SNATVirtualMachine(object):
    HYPERV_GENERATION = '2'
    RAM_GB = '1GB'

    USERNAME = 'ubuntu'
    PASSWORD = 'ubuntu'

    INJECT_IP_SCRIPT_REL_PATH = "vrouter_hyperv_inject_ip.ps1"
    WAIT_FOR_VM_TIME_SEC = 5
    NUM_INJECT_RETRIES = 60

    HOST_MGMT_IP = "169.254.150.1"
    MGMT_PREFIX_LEN = 16
    MGMT_SUBNET_MASK = "255.255.0.0"

    def __init__(self, vm_uuid, nic_left, nic_right, wingw_vm_name=None,
                 vm_location=None, vhd_path=None, forwarding_mac=None):
        self.vm_uuid = vm_uuid
        self.nic_left = nic_left
        self.nic_right = nic_right
        self.vm_location = vm_location
        self.vhd_path = vhd_path
        if wingw_vm_name is None:
            self.wingw_name = WINGW_PREFIX + self.vm_uuid
        else:
            self.wingw_name = wingw_vm_name
        root_disk_dir = os.path.dirname(self.vhd_path)
        cloned_disk_name = "disk_" + self.vm_uuid.split("-")[0] + ".vhdx"
        self.cloned_disk_path = os.path.join(root_disk_dir, cloned_disk_name)
        # TODO: Remove `forwarding_mac` when agent is functional
        self.forwarding_mac = forwarding_mac

    def create(self, mgmt_vswitch_name):
        """calls powershell to create vm """
        if self._exists():
            raise ValueError("Specified Windows gateway VM already exists")
        self._configure_host_mgmt_ip(mgmt_vswitch_name)
        call_powershell(["Copy-Item", self.vhd_path, self.cloned_disk_path])
        call_powershell(["New-VM", "-Name", self.wingw_name, \
                         "-Path", self.vm_location, \
                         "-Generation", self.HYPERV_GENERATION, \
                         "-MemoryStartupBytes", self.RAM_GB, \
                         "-VHDPath", self.cloned_disk_path, \
                         "-SwitchName", mgmt_vswitch_name])
        call_powershell(["Set-VMFirmware", "-VMName", self.wingw_name, \
                         "-EnableSecureBoot", "Off"])

    def spawn(self):
        """starts the VM"""
        call_powershell(["Start-VM", "-Name", self.wingw_name])

    def cleanup(self):
        """calls powershell to destroy vm and remove cloned vhdx disk"""
        if not self._exists():
            raise ValueError("Specified Windows gateway VM does not exist")
        call_powershell(["Stop-VM", "-Name", self.wingw_name, "-Force"])
        call_powershell(["Remove-VM", "-Name", self.wingw_name, "-Force"])
        call_powershell(["Remove-Item", self.cloned_disk_path])

    def inject_ip(self, mgmt_ip, mgmt_switch_name):
        """injects management IP to the vm"""
        this_script_path = os.path.realpath(__file__)
        this_script_dir = os.path.dirname(this_script_path)
        inject_ip_script_path = os.path.join(this_script_dir,
                                             self.INJECT_IP_SCRIPT_REL_PATH)
        retry_num = 0
        while retry_num < self.NUM_INJECT_RETRIES:
            retry_num += 1
            time.sleep(self.WAIT_FOR_VM_TIME_SEC)
            try:
                call_powershell(["&", inject_ip_script_path,
                                 "-SwitchName", mgmt_switch_name,
                                 "-Name", self.wingw_name,
                                 "-IPAddress", str(mgmt_ip),
                                 "-Subnet", self.MGMT_SUBNET_MASK])
                call_powershell(["ping", str(mgmt_ip), "-n", "1"])
            except subprocess.CalledProcessError:
                continue
            else:
                break
        if retry_num == self.NUM_INJECT_RETRIES:
            raise RuntimeError("Waited for SNAT VM for too long")

    def attach_vrouter(self, vswitch):
        """attaches left and right NICs of the VM to the vrouter switch"""
        call_powershell(["Add-VMNetworkAdapter", "-VMName", self.wingw_name,
                         "-SwitchName", vswitch,
                         "-Name", self.nic_left.win_name])
        call_powershell(["Add-VMNetworkAdapter", "-VMName", self.wingw_name,
                         "-SwitchName", vswitch,
                         "-Name", self.nic_right.win_name])

    def set_snat(self, mgmt_ip):
        """sshs into gateway machine and configures SNAT"""
        ssh_client = None
        try:
            ssh_client = BlockingSSHClient()
            ssh_client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
            self._connect_with_retries(ssh_client, mgmt_ip)
            self._setup_snat(ssh_client)
        finally:
            if ssh_client:
                ssh_client.close()

    def register(self):
        """registers VM ports to Contrail"""
        self.nic_left.register()
        self.nic_right.register()

    def unregister(self):
        """unregisters VM ports from Contrail"""
        self.nic_left.unregister()
        self.nic_right.unregister()

    def _exists(self):
        """calls powershell to check whether vm exists"""
        try:
            call_powershell(["Get-VM", "-Name", self.wingw_name, "-ErrorAction",
                             "SilentlyContinue", "|", "Out-Null"])
        except subprocess.CalledProcessError:
            return False
        return True

    def _configure_host_mgmt_ip(self, adapter_name):
        """configures host management adapter and its IP"""
        current_ip = call_powershell(["Get-NetAdapter", "|", "Where-Object",
                                      "Name", "-Match", adapter_name, "|",
                                      "Get-NetIPAddress", "|", "Where-Object",
                                      "AddressFamily", "-eq", "IPv4", "|",
                                      "Select", "-ExpandProperty", "IPAddress"])
        if current_ip != self.HOST_MGMT_IP:
            if_index = call_powershell(["Get-NetAdapter", "|",
                                        "Where-Object", "Name", "-Match",
                                        adapter_name, "|"
                                        "Select", "-ExpandProperty", "ifIndex"])
            if current_ip != "":
                call_powershell(["Get-NetAdapter", "|", "Where-Object",
                                 "Name", "-Match", adapter_name, "|"
                                 "Remove-NetIPAddress", "-Confirm:$false"])
            call_powershell(["New-NetIPAddress", "-IPAddress",
                             self.HOST_MGMT_IP, "-PrefixLength",
                             str(self.MGMT_PREFIX_LEN), "-InterfaceIndex",
                             if_index])

    def _connect_with_retries(self, ssh_client, mgmt_ip):
        attempts = 0
        while True:
            try:
                ssh_client.connect(str(mgmt_ip),
                                   username=self.USERNAME,
                                   password=self.PASSWORD,
                                   auth_timeout=5)
                break
            except socket.error:
                attempts += 1
                if attempts >= 5:
                    break
                else:
                    raise

    def _setup_snat(self, ssh_client):
        ssh_client.exec_command("sudo sysctl -w net.ipv4.ip_forward=1")
        ssh_client.exec_command("sudo ip link set dev {} up"
                                .format(self.nic_left.name))
        ssh_client.exec_command("sudo ip link set dev {} up"
                                .format(self.nic_right.name))
        ssh_client.exec_command("sudo ip addr add dev {} {}"
                                .format(self.nic_left.name,
                                        self.nic_left.gw_cidr))
        ssh_client.exec_command("sudo ip addr add dev {} {}"
                                .format(self.nic_right.name,
                                        self.nic_right.gw_cidr))
        ssh_client.exec_command("sudo iptables -t nat -F")
        ssh_client.exec_command("sudo iptables -t nat -A POSTROUTING -o {} "
                                "-j MASQUERADE"
                                .format(self.nic_right.name))
        ssh_client.exec_command("sudo iptables -A FORWARD -i {} -o {} -m state "
                                "--state RELATED,ESTABLISHED -j ACCEPT"
                                .format(self.nic_right.name,
                                        self.nic_left.name))
        ssh_client.exec_command("sudo iptables -A FORWARD -i {} -o {} -j ACCEPT"
                                .format(self.nic_left.name,
                                        self.nic_right.name))
        ssh_client.exec_command("sudo /etc/init.d/iptables-persistent save")

        # TODO: Remove when Agent is functional
        ssh_client.exec_command("sudo arp -i {} -s 10.7.3.10 {}"
                                .format(self.nic_right.name,
                                        self.forwarding_mac))


class VRouterHyperV(object):
    """
    Create or destroy a Hyper-V Gateway VM for NAT between two virtual
    networks.
    """

    def __init__(self):
        self._parse_args(sys.argv[1:])

    def _parse_args(self, args):
        """Return an argparse.ArgumentParser for me"""
        conf_parser = argparse.ArgumentParser(add_help=False)

        _, remaining_argv = conf_parser.parse_known_args(args)
        # Override with CLI options
        # Don't surpress add_help here so it will handle -h
        parser = argparse.ArgumentParser(
            # Inherit options from config_parser
            parents=[conf_parser],
            # print script description with -h/--help
            description=__doc__,
            # Don't mess with format of description
            formatter_class=argparse.RawDescriptionHelpFormatter,
        )
        subparsers = parser.add_subparsers()

        create_parser = subparsers.add_parser('create')
        create_parser.add_argument(
            "--vm_location",
            required=True,
            help="Location of gateway VM")
        create_parser.add_argument(
            "--vhd_path",
            required=True,
            help="Path of VHD of VM")
        create_parser.add_argument(
            "--mgmt_vswitch_name",
            required=True,
            help="Name of management virtual switch")
        create_parser.add_argument(
            "--vrouter_vswitch_name",
            required=True,
            help="Name of vRouter virtual switch")
        create_parser.add_argument(
            "vm_id",
            help="Virtual machine UUID")
        create_parser.add_argument(
            "vmi_left_id",
            help="Left virtual machine interface UUID")
        create_parser.add_argument(
            "vmi_right_id",
            help="Right virtual machine interface UUID")
        create_parser.add_argument(
            "--vmi-left-mac",
            default=None,
            help=("Left virtual machine interface MAC. Default: automatically "
                  "generated by the system"))
        create_parser.add_argument(
            "--vmi-left-ip",
            default=None,
            help=("Left virtual machine interface IPv4 and mask "
                  "(ie: a.a.a.a/bb). Default mask to /32"))
        create_parser.add_argument(
            "--vmi-right-mac",
            default=None,
            help=("Right virtual machine interface MAC. Default: "
                  "automatically generated by the system"))
        create_parser.add_argument(
            "--vmi-right-ip",
            default=None,
            help=("Right virtual machine interface IPv4 and mask "
                  "(ie: a.a.a.a/bb). Default mask to /32"))
        create_parser.add_argument(
            "--left-gw-cidr",
            default=None,
            help=("Gateway IP for left network"))
        create_parser.add_argument(
            "--right-gw-cidr",
            default=None,
            help=("Gateway IP for right network"))
        # TODO: Remove when Agent is functional
        create_parser.add_argument(
            "--forwarding-mac",
            default=None,
            help=("MAC address of the forwarding interface on host"))
        create_parser.set_defaults(func=self.create)

        destroy_parser = subparsers.add_parser('destroy')
        destroy_parser.add_argument(
            "vm_id",
            help="Virtual machine UUID")
        destroy_parser.add_argument(
            "vmi_left_id",
            help="Left virtual machine interface UUID")
        destroy_parser.add_argument(
            "vmi_right_id",
            help="Right virtual machine interface UUID")
        destroy_parser.add_argument(
            "--vhd_path",
            required=True,
            help="Path of VHD of VM")
        destroy_parser.set_defaults(func=self.destroy)

        self.args = parser.parse_args(remaining_argv)

    def create(self):
        """Creates and configures a SNAT VM"""
        vm_id = validate_uuid(self.args.vm_id)

        nic_left = None
        if uuid.UUID(self.args.vmi_left_id):
            nic_left = VMPort(uuid=self.args.vmi_left_id,
                              prefix=LEFT_DEV_PREFIX,
                              mac=self.args.vmi_left_mac,
                              ip_cidr=self.args.vmi_left_ip,
                              gw_cidr=self.args.left_gw_cidr)

        nic_right = None
        if uuid.UUID(self.args.vmi_right_id):
            nic_right = VMPort(uuid=self.args.vmi_right_id,
                               prefix=RIGHT_DEV_PREFIX,
                               mac=self.args.vmi_right_mac,
                               ip_cidr=self.args.vmi_right_ip,
                               gw_cidr=self.args.right_gw_cidr)

        # TODO: Remove `forwarding_mac` when Agent is functional
        snat_vm = SNATVirtualMachine(vm_id, nic_left, nic_right,
                                     forwarding_mac=self.args.forwarding_mac,
                                     vm_location=self.args.vm_location,
                                     vhd_path=self.args.vhd_path)
        snat_vm.create(self.args.mgmt_vswitch_name)
        try:
            snat_vm.attach_vrouter(self.args.vrouter_vswitch_name)
            snat_vm.spawn()

            ipam = MgmtIPAM(self.args.mgmt_vswitch_name)
            mgmt_ip = ipam.generate()
            snat_vm.inject_ip(mgmt_ip, self.args.mgmt_vswitch_name)

            snat_vm.set_snat(mgmt_ip)
            snat_vm.register()
        except:
            snat_vm.cleanup()
            raise

    def destroy(self):
        """Destroys a SNAT VM"""
        vm_id = validate_uuid(self.args.vm_id)

        nic_left = None
        if uuid.UUID(self.args.vmi_left_id):
            nic_left = VMPort(uuid=self.args.vmi_left_id,
                              prefix=LEFT_DEV_PREFIX)

        nic_right = None
        if uuid.UUID(self.args.vmi_right_id):
            nic_right = VMPort(uuid=self.args.vmi_right_id,
                               prefix=RIGHT_DEV_PREFIX)

        snat_vm = SNATVirtualMachine(vm_id, nic_left, nic_right,
                                     vhd_path=self.args.vhd_path)
        try:
            snat_vm.unregister()
        finally:
            snat_vm.cleanup()


def main():
    vrouter_hyperv = VRouterHyperV()
    vrouter_hyperv.args.func()


if __name__ == "__main__":
    main()
