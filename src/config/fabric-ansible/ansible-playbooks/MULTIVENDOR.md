# Multi-Vendor Fabric Automation

This document quickly summarizes the approach behind multi-vendor support in Contrail Networking.

## Device Discovery

Currently, the `device_info.py` module attempts to perform classification via SNMP. The `group_vars/all.yml`
file contains a list that maps SNMP OIDs to vendor names.

In the event that a network device doesn't have SNMP enabled, or there's some kind of issue performing classification
via SNMP, this module will fall back to sending a set of commands via SSH. However, the existing commands assume a
Junos XML response, which means that if the device a) is not Junos and b) doesn't have SNMP enabled, it won't be
managed via Contrail Networking..

This patch proposes adding two components to the device discovery playbook and module:

- Netmiko for [SSH-based multivendor classification](https://github.com/ktbyers/netmiko/blob/develop/netmiko/ssh_autodetect.py). This currently supports numerous vendors, and is a well-maintained, active library for working with network devices via SSH
- [NAPALM](https://github.com/napalm-automation/napalm) for multi-vendor management. This is also a very active project for being able to manage network devices in a multi-vendor fashion. Junos as well as many other network operating systems are supported by this library.

Logically, the existing SNMP classification method works exactly the same. Users can still add OID mappings to the `group_vars/all.yml` file, and if SNMP is enabled and the module can connect, the fabric object will be updated with the appropriate vendor name.

However, if SNMP fails, it will fall back to `netmiko` for classification. If `netmiko` is able to successfully determine the vendor, this module will do two things:

- Automatically configure the device over SSH (`netmiko`) to enable a NAPALM-compatible API, using the commands under `group_vars` in `enable_api_commands`. This behavior is enabled by default, but can be disabled using the `group_vars` variable `autoenable_api`.
- Test connectivity via NAPALM to the network device by retrieving device facts. This ensures that future management activities can take place via NAPALM.

## Fabric Automation

TBD, as this hasn't been submitted in this patch yet. However, since this patch does allow us to determine the
NAPALM driver name, this shouldn't be too complicated, as the NAPALM library and Ansible modules provide
multi-vendor capabilities, and we'll be moving to use those in a future patch.

## Demo

My [`tftf`](https://github.com/Mierdin/tftf/) repo holds a few scripts and cloudformation templates based off of the existing Tungsten Fabric CloudFormation work, but that also adds an Arista EOS image for testing:

```
git clone https://github.com/Mierdin/tftf/ && cd 
aws cloudformation create-stack --capabilities CAPABILITY_IAM --stack-name tf --template-body file://cftemplate-arista.yaml
```

Follow my [blog here](https://keepingitclassless.net/2018/05/up-running-kubernetes-tungsten-fabric/) for detailed info on how to connect to the instances to test this out.

### Integrate Playbook Changes

This is the workflow I used to get my changes onto the TF installation, maybe it works for you.

`/var/log/contrail` is mounted as a volume to the `config_api_1` container, so we'll clone the repo there, and then cp into the right place If you ssh into tf-controller01, you'll be able to access this container and run these commands to update the container with the new playbooks:

```
# One time
yum install git -y
cd /var/log/contrail/
sudo git clone https://github.com/Mierdin/contrail-controller
sudo docker exec config_api_1 pip install netmiko napalm
sudo docker exec config_api_1 rm -rf /opt/contrail/fabric_ansible_playbooks/
sudo docker exec config_api_1 cp -r /var/log/contrail/contrail-controller/src/config/fabric-ansible/ansible-playbooks /opt/contrail/fabric_ansible_playbooks/
```

### Set up Fabric instance and trigger playbook job

I also wrote a few scripts to easily let me tear down and recreate the appropriate objects and trigger the device discovery process.

Go back to the `tftf` directory on your local machine, and edit `lib/const.py` as appropriate for your environment. Then install the pip dependencies and run the script:

```
pip install requests 
python define_fabric.py
```

You'll see some print statements showing the various things being created/deleted in order to trigger the job to run the playbooks:

```
 ❯ python3 define_fabric.py                                                                                                                                                                                                    [20:16:49]
Created fabric 8b4e6625-75ff-470c-a88c-f8f1cba0c3f3
Created fabric namespace 9403a1df-0d13-46d9-99e9-2ce85c63bbbd
Executed job 392f751b-e678-4042-a8b6-c2cbc4298db2
{'display_name': 'foobar_fabric', 'uuid': '8b4e6625-75ff-470c-a88c-f8f1cba0c3f3', 'href': 'http://ec2-18-237-121-198.us-west-2.compute.amazonaws.com:8082/fabric/8b4e6625-75ff-470c-a88c-f8f1cba0c3f3', 'fabric_credentials': {'device_credential': [{'credential': {'username': 'admin', 'password': 'arista'}, 'vendor': 'arista'}]}, 'perms2': {'owner': 'cloud-admin', 'owner_access': 7, 'global_access': 0, 'share': []}, 'id_perms': {'enable': True, 'uuid': {'uuid_mslong': 10038072930534901516, 'uuid_lslong': 12145356012498502643}, 'created': '2018-06-18T03:16:50.545781', 'description': None, 'creator': None, 'user_visible': True, 'last_modified': '2018-06-18T03:16:50.545781', 'permissions': {'owner': 'cloud-admin', 'owner_access': 7, 'other_access': 7, 'group': 'cloud-admin-group', 'group_access': 7}}, 'fq_name': ['default-global-system-config', 'foobar_fabric'], 'fabric_namespaces': [{'to': ['default-global-system-config', 'foobar_fabric', 'foobar_fns'], 'href': 'http://ec2-18-237-121-198.us-west-2.compute.amazonaws.com:8082/fabric-namespace/9403a1df-0d13-46d9-99e9-2ce85c63bbbd', 'uuid': '9403a1df-0d13-46d9-99e9-2ce85c63bbbd'}], 'name': 'foobar_fabric'}
> /Users/mierdin/Code/Juniper/tftf/define_fabric.py(59)main()
-> fabric = get_fabric("foobar_fabric")
(Pdb)
```

At the end you'll see (pdb), this is a breakpoint. Wait here and look at the logs on `tf-controller01`. You'll see something like this:

```
06/18/2018 03:17:05.858 [device_info] pid=8674 [INFO]:  Number of greenlets: 1 and total_percent: 80.8
06/18/2018 03:17:05.858 FabricAnsible [INFO]:  success_task_percent 80.8 failed_task_percent None
06/18/2018 03:17:05.858 [device_info] pid=8674 [INFO]:  Per greenlet percent: 80.8
06/18/2018 03:17:05.863 requests.packages.urllib3.connectionpool [INFO]:  Starting new HTTP connection (1): 127.0.0.1
06/18/2018 03:17:05.864 FabricAnsible [INFO]:  __default__ [SYS_INFO]: JobLog: log_entry = <<  name = default-global-system-config:discover_device_template  execution_id = 392f751b-e678-4042-a8b6-c2cbc4298db2  timestamp = 1529291825858  message = Prefix(es) to be discovered: 10.10.10.232/32  status = IN_PROGRESS  fabric_name = default-global-system-config:foobar_fabric  percentage_completed = 9.5  >>
06/18/2018 03:17:05.866 requests.packages.urllib3.connectionpool [INFO]:  Starting new HTTP connection (1): 10.10.10.11
06/18/2018 03:17:05.888 [device_info] pid=8674 [INFO]:  HOST 10.10.10.232: REACHABLE
06/18/2018 03:17:17.951 [device_info] pid=8674 [INFO]:  SNMP failed for host 10.10.10.232 with error No SNMP response received before timeout
06/18/2018 03:17:17.951 [device_info] pid=8674 [INFO]:  Attempting to get device info via netmiko and NAPALM
06/18/2018 03:17:18.068 paramiko.transport [INFO]:  Connected (version 2.0, client OpenSSH_6.6.1)
06/18/2018 03:17:18.264 paramiko.transport [INFO]:  Authentication (keyboard-interactive) successful!
06/18/2018 03:17:31.118 [device_info] pid=8674 [INFO]:  Netmiko discovery results: {'netmiko_driver': u'arista_eos', 'napalm_driver': 'eos'}
06/18/2018 03:17:31.237 paramiko.transport [INFO]:  Connected (version 2.0, client OpenSSH_6.6.1)
06/18/2018 03:17:31.389 paramiko.transport [INFO]:  Authentication (keyboard-interactive) successful!
06/18/2018 03:17:39.705 [device_info] pid=8674 [INFO]:  Device created with uuid- 10.10.10.232 : 319801b3-d816-4e7b-88d0-78043f4e1e82
06/18/2018 03:17:39.707 FabricAnsible [INFO]:  __default__ [SYS_INFO]: PRouterOnboardingLog: log_entry = <<  name = default-global-system-config:localhost  timestamp = 1529291859706  onboarding_state = DISCOVERED  job_template_fqname = default-global-system-config:discover_device_template  job_execution_id = 392f751b-e678-4042-a8b6-c2cbc4298db2  job_input = {"fabric_uuid": "8b4e6625-75ff-470c-a88c-f8f1cba0c3f3"}  >>
06/18/2018 03:17:39.708 FabricAnsible [INFO]:  __default__ [SYS_INFO]: JobLog: log_entry = <<  name = default-global-system-config:discover_device_template  execution_id = 392f751b-e678-4042-a8b6-c2cbc4298db2  timestamp = 1529291859706  message = Discovered device details: 10.10.10.232 : localhost : None  status = IN_PROGRESS  fabric_name = default-global-system-config:foobar_fabric  percentage_completed = 80.8  >>
06/18/2018 03:17:39.730 [device_info] pid=8674 [INFO]:  Fabric updated with physical router info for host: 10.10.10.232
06/18/2018 03:17:39.730 FabricAnsible [INFO]:  success_task_percent 4.75 failed_task_percent 0.0
06/18/2018 03:17:39.732 FabricAnsible [INFO]:  __default__ [SYS_INFO]: JobLog: log_entry = <<  name = default-global-system-config:discover_device_template  execution_id = 392f751b-e678-4042-a8b6-c2cbc4298db2  timestamp = 1529291859731  message = Device discovery complete  status = IN_PROGRESS  fabric_name = default-global-system-config:foobar_fabric  percentage_completed = 4.75  >>
2018-06-18 03:17:39,995 [ansible] pid=8523 ok: [localhost]
2018-06-18 03:17:39,999 [ansible] pid=8523 TASK [set output parameter] ****************************************************
2018-06-18 03:17:40,024 [ansible] pid=8523 ok: [localhost]
2018-06-18 03:17:40,028 [ansible] pid=8523 TASK [print output] ************************************************************
2018-06-18 03:17:40,049 [ansible] pid=8523 ok: [localhost] => {
    "output": {
        "message": "Discover playbook successfully executed",
        "status": "SUCCESS"
    }
}
2018-06-18 03:17:40,050 [ansible] pid=8523 PLAY RECAP *********************************************************************
2018-06-18 03:17:40,050 [ansible] pid=8523 localhost                  : ok=18   changed=0    unreachable=0    failed=0
```

Go back to the script and hit `c` to continue, and you'll see a printout of the current `prouter` variable, which shows our fabric has a physical router being managed:

```
(Pdb) c
--Return--
> /Users/mierdin/Code/Juniper/tftf/define_fabric.py(61)main()->None
-> prouters = fabric['physical_router_refs']
(Pdb) prouters
[{'to': ['default-global-system-config', 'localhost'], 'href': 'http://ec2-18-237-121-198.us-west-2.compute.amazonaws.com:8082/physical-router/319801b3-d816-4e7b-88d0-78043f4e1e82', 'attr': None, 'uuid': '319801b3-d816-4e7b-88d0-78043f4e1e82'}]
```

We can also now see this device in the Contrail UI.
