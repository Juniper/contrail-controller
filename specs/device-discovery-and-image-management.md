
# 1. Introduction
Contrail Fabric management use cases require management and automation of the underlay networks in the data center. This spec covers the design and implementation of extending existing Contrail config node to provide EMS capabilities on physical network elements, such as TOR/EOR switches, Spines, SDN gateway, and VPN gateways in the data center. User should be able to perform basic device management functions such device discovery, inventory import, image management, device image upgrade, etc. 

# 2. Problem statement
Contrail currently does not manage the underlay network in the data center and it assumes the underlay network is provisioned prior to the overlay network provisioning via Contrail. There is no automation of the underlay network provisioning and management. Because the underlay network is managed outside Contrail, admin needs to manually populate the Contrail VNC data model with underlay network info via UI or API. This process could be automated when the underlay network is managed by Contrail.

Many underlay network is built with devices from multiple vendors. One of the challenge to underlay network management is multi-vendor support. Each vendor's device has its own CLI commands, configuration schema, or APIs to configure and operate. 

Here are the epic user stories that captures the requirements from PLM for underlay network onboarding and automation.

#### [User Story GLAC-2] Importing/Discovering DC Fabric and Service Objects 
This User Epic refers to a Greenfield or Brownfield Data Center, where a new set of devices has been installed and powered on. These devices are TORs, Spines, VCF configured collapsed TOR/Spines, SDN Gateways, VPN gateways,  or Bare Metal Servers. The user wants to be able to automate the setup/expansion of the DC Fabric,  and at the same time to be able to use Contrail to create virtual networks/overlays across vRouter-based workloads and Bare Metal servers/non-vRouter based workloads.

To achieve this goal the user wants bring these new devices and servers under the management of Contrail Fabric with minimal user intervention, possibly none when setting up or expanding a fabric or a set of Bare Metal servers.

This user Epic covers the discovery and import of devices, before any action (re-imaging, configuration, role assignment) is performed.

#### [User Story GLAC-110] Perform software upgrade on selected devices
As a DC operator, the user must be able from the Contrail Fabric UI to 
1) select a set of devices by group,type,vendor/model and/or OS version 
2) apply a polocy that performs SW upgrade on the selected devices to a target OS version. 

Acceptance criteria: All selected devices are software upgraded to the target OS image selected

#### [User Story GLAC-47] Device Onboarding - brown field
The user wants to onboard a Brownfield device in Contrail Fabric Onboarding the device means to change its status to Ready-For-Service, which means the device can be used to perform CRUDL operations on Overlay services 

Initial condition: The device is present in the Device Inventory where it must have at least Mgmt IP @ assigned (& associated mgmt MAC), Vendor:type:OS, Fabric name assigned, GroupID assigned 

The device has already management address, base configuration active. It also has IGP protocols , routing policies configured. However it may not have role-specific fabric configuration 


#### [User Story GLAC-136] Device Topology Discovery

# 3. Proposed solution
The proposed solution is to extend Contrail config node with EMS functionality via Ansible. Diagram below shows the high level design of the propose solution.

![High Level Design](images/fabmgt_ems.png)

#### Why Ansible?
- Multi vendor support with rich plugin
- Extensible configuration management and automation with rich set of plugins 
- Easy to customize by network engineer during deployment

#### Why job?
- Some EMS functions such as image upgrade or RMA could take long time to execute and user needs feedback on the progress
- Many EMS functions are applied to multiple devices in a workflow. It is important for admin to keep track of devices with failures.
- There are use cases where EMS functions needs to be scheduled to run at certain time window.

#### Ansible Playbook Execution Flow
![Job call flow](images/fabmgt_pb_flow.png)

## 3.1 Alternatives considered
#### NA

## 3.2 API schema changes
### 3.2.1 Device and Image Management
#### device-image 
This is a new identity object added to the VNC data model to capture all the metadata for the device image. When device image object is assigned to the physical-router, the intent is to upgrade the physical-router to the specific software version specified in the device image object. When the image upgrade is successfully performed, the UvePhysicalRouterConfigTrace UVE's os_version attribute should match the one defined in the device image. Here are the list of properties for the device image object:

#### link-aggregation-group

#### Updated VNC identities:
global-system-config, physical-router, physical-interface, logical-interface, 

#### Updated UVEs:
UvePhysicalRouterConfigTrace

![Device and Image Management](images/fabmgt_dm.png)


### 3.2.1 EMS Ansible Jobs
![Job Management](images/fabmgt_job_dm.png)

#### New VNC identities
job, job-template
#### New UVE and object logs
JobExecutionUVE, JobLog

## 3.3 User workflow impact
#### (pending to discussion with UI team)

## 3.4 UI changes
#### Introduction
For Phase 1, the goal is to seemlessly integrate the new functionality required by the UI, into the existing Contrail UI. There are several new areas or UI screens that need to be added as follows:

###### Fabric Page
A new section where the user can create a new "fabric" and specify one or more "namespaces" is required. The left navigation can be extended by adding a new section called __Fabric__. Once selected, a statistical 
landing page will be displayed, with information about the various fabrics and devices (details TBD). A list of existing fabrics will be displayed, along with an __Add__ button to add a new fabric. 
Once a fabric has been selected, the user can then do the following:
- create a new namespace
- edit an existing namespace with the caveat that deletion is not permitted and needs to be handled elsewhere.
- Once a namespace is created, the user has the option to run a __Discovery__ on that namespace.
- Discovery needs to be idempotent


###### Devices Page
Under the devices page, two new columns need to be added; __state__ and __role__. The __states__ indicates the state that the device is in:

- __Probing__ - this state is set when a ping/snmp has determined that it is a device of some sort, but before the credentials have been tried.
- __Credentials Failed__ - this state indicates that none of the credentials worked. Here the user may go back to the fabric page and add an additional credential
to one of the namespaces.
- __Under Config Pending__ - this state indicates that the device is under fabric management, but the underlay hasn't been configured
- __Under Management__ - the underlay has been configured and the device is under fully under management and can now be used by the overlay.
- _please note the above states are not final._

The __role__ specifies whether that device is a _spine_ or a _leaf_ and the cell within that row is editable with a droplist of either option. Once a role has been selected and saved,
a job is automatically triggered that calls an Ansible playbook specific to that device type and role it plays to configure the _underlay configuration_. Once the job completes successfully,
the __state__ will refelct __Underlay Configured__. 

###### Job Services Page
A new __Job Services__ page will be added that shows all running jobs and previously run jobs. A percentage completion will be displayed, along with a details page to show detailed progress. The job service
will break up jobs into multiple greenlets, in batches of 20 where each greenlet will run against a single device. The overall job status will indicate __success__ or __failure__ with failure indicating
that some error/problem occurred, for example: 

- either an ansible playbook failed to execute (crashed or some ansible exception occurred)
- a push of a config did not succeed either because
    - there was no netconf channel established
    - the configuration pushed to the device return an error
    - etc
- the job detailed log needs to indicate 3 things:
    - what the error was
    - why did it occur
    - how to fix it


## 3.5 Notification impact
#### Review links:
https://review.opencontrail.org/38468

# 4. Implementation
## 4.1 Device Discovery
In a brownfield scenario, the assumption is that the devices have a management IP address already configured. Prior to device discovery, the user would have created a namespace. A namespace consists of a collection of 
IP prefixes, IP address ranges, along with several pairs of credentials. One or more namespaces can be created with overlapping and/or different IP address ranges and credentials. Device discovery is triggered by the 
user where the user can select either a namespace, or can manually enter IP address ranges and prefixes along with credentials.

1) A _probe_devices_ job is issued and an _execute_job_ call is issued to the API server along with input parameters such as a json body (with IP prefixes, credentials, namespace), a job template ID.
2) An Ansible playbook/module will then: 
    - ping the device list from the namespace data, if the ping responds, an SNMP get system info will be issued to determine the device type and family.
    - If SNMP OID indicates that it's a Juniper family, store that information (IP address, device type/family) into the database _create_pr_, setting the state to _probing_
    - If the SNMP OID indicates that it's a non-Juniper device, discard.
    - Continue for all namespace information
    - Take all the Juniper found devices and try the credentials.
    - If one of the credentials works, update the PR with that information and set the PR state to _probed_
    - If the device was a Juniper device, but none of the credentials work, set the PR state to _credentials_failed_
3) Job logs are recorded at every level.

During device discovery, the PR state will transition as follows:
- Probing - indicates that the device has passed the SNMP stage and the discovery is now trying the various credentials.
- Credentials Failed - indicates that none of the credentials worked.
- Under Management - indicates that the device has been discovered and is under management and the credentials worked
- Underlay Configured - indicates that the underlay has been configured/set for this device.



## 4.2 Device Import

After device discovery has completed and the device is in a state of "under management", the device configuration is then imported. This action is triggered via the UI where the user can select all "under management" 
and then select Device Import

A _device_import_ job is launched with the subset of newly discovered Juniper devices and an Ansible playbook will:
- import the device configuration
- The following configurations will be imported:
    - physical interfaces
    - Logical interfaces
    - VLAN information
    
The configuration information will be reflected in the various UI screens like the physical router page, physical interface, and logical interface pages, etc.

The input json schema for the _device_import_ module is:

```
{
    "$schema": "http://json-schema.org/draft-06/schema#",
    "title": "Device Import Input",
    "type": "object",
    "properties": {
        "prouter_uuid": {
        "type": "string",
        "pattern":"^[a-fA-F0-9]{8}-[a-fA-F0-9]{4}-4[a-fA-F0-9]{3}-[89aAbB][a-fA-F0-9]{3}-[a-fA-F0-9]{12}$"
        }
    },
    "required": ["prouter_uuid"]
}
 ```

## 4.3 Image Upgrade
#### Image Upload to Fabric Manager
- The user uploads a selected image through the UI. The UI server will upload the file to the Swift server and is driven from the UI via multi-part html.
- A URL is returned after the image is uploaded to Swift.
- The UI server will call _create_image_ object with all the metadata such as the image URL, device family, vendor information using the API server. 

#### Image Upgrade
1) To upgrade an image, the new image is first selected from a list of available images.
2) Based on the image selection, a list of compatible devices are displayed. The user is allowed to choose one or multiple devices from the list to upgrade.
3) An _image_upgrade_ job is issued with the image ID and a device list.
4) An _execute_job_ call is issued to the API server along with input parameters such as a json body(with image UUID and list of device UUIDs), a job template ID (an _image_upgrade_ job 
template that contains a list of all the _image_upgrade_ playbooks for various device families.)
5) From the API server, the job execution is handled by the Job Manager, which analyses the input json and selects the right playbook to be executed from the list of image_upgrade Ansible playbooks.
6) The Job Manager will spawn the threads for each device. Each thread will run a playbook for each device.
7) The Job Manager will log all the results of each playbook in the job log and will aggregate a summary for the main job.
8) Any exceptions in the playbook will be caught by the Job Manager and will be logged accordingly in job logs.

Below is the json schema for the _image_upgrade_ job template:

```
{
    "$schema": "http://json-schema.org/draft-06/schema#",
    "title": "Image upgrade input",
    "type": "object",
    "properties": {
        "image_uuid": {
            "type": "string"
        }
    },
    "required": ["image_uuid"]
}
```

 
#### Playbook Details
1) The Playbook will communicate with the API server to get the image metadata and PR (Physical Router) information.
2) The Device family information contained in the Image metadata is validated with the PR device family information for compatibility.
3) Then the playbook needs to connect to the Swift server to access the selected image. In order to do that, an _etcd_ file has to be written with all the required credentials and access 
details. __Under NO circumstances will credentials be stored in clear text.__ The same config would have been used by the UI server at the time of upload. A non-authenticated temp URL of the image file is requested from the Swift file server.
4) The image will then be upgraded on the device. This involves the following steps:
    - __Staging__: Copying the file from Swift to the device using the Swift temp URL. This is done using http file-copy command on the device. All the checksum validations are done.
    - __Upgrade__: This is done using “request software add” command.
    - __Reboot__: Reboot is issued after image upgrade is done.
    - __Validation__: After the device boots up we check for the version and validate.  
5) The PR UVE  for the device will be updated with the OS version.
6) Job logs are also recorded at every level.
7) If the image upgrade fails, the failure is captured in the logs
 
###### Notes
- The etcd file for Swift configurations has to be constructed with the help of the provisioning team.
- We will use _juniper_junos_software_ Ansible library for all Junos device family.


## 4.4 Topology Discovery

## 4.5 Underlay Configuration
For phase 1, the underlay configuration will be triggered by the user through the UI. The user will select the newly discovered devices that have been brought into fabric manager, and trigger an underlay configuration job. 

An _underlay_config_ job is created and launched and an Ansible playbook will then push the following configuration knobs:
- LLDP Configuration
    - Only push LLDP configuration if the flag is enabled (specified by the user)
    - Contrail analytics node discovers Layer 2 topology connectivity via LLDP
- Netconf (CLI)
- Interface IPs, 
- Syslog
- NTP
- Routing options like router ID
- BGP

## 4.6 Job Service
_Needs more work, Tong to update_

Long running requests will need to take advantage of the new Job Service infrastructure, along with a new UI where the user can see all running jobs and past run jobs. The Job service will read the 
job template object and based on the parameter 'Multi_device_job', will spawn a single greenlet or use a pool of greenlets with a concurrency set to 20. Each greenlet works on a single device. The greenlet 
will internally invoke the playbook and there is a timeout to guard the playbook
execution. The Job service will aggregate the various playbook outputs and create a job log for capturing the job task summary.

All the playbooks must return the output based on the schema below. The internals of the optional "details" field can vary for different playbooks based on the logic and the consumer of the playbook output.

```
{
    "$schema": "http://json-schema.org/draft-06/schema#",
    "title": "Generic Job Output",
    "type": "object",
    "properties": {
        "status": {
            "type": "string",
            "enum": ["Success", "Failure"],
            "description": "Result status of the job"
        },
        "message": {
            "type": "string",
            "description": "Should capture a summarized error message in case of Failures."
        },
        "details": {
            "type": "object",
            "description": "JSON object holding the job specific output details"
        }
    },
    "required": ["status"]
}
```



## 4.6 Ansible Modules
### VNC Ansible Module
### UVE Ansible Module
### Object Log Ansible Module

# 5. Performance and scaling impact
## 5.1 API and control plane
#### TBD

## 5.2 Forwarding performance
#### NA

# 6. Upgrade
#### backward compatible schema changes

# 7. Deprecations
#### No deprecations

# 8. Dependencies
#### Current overlay config generation in DM.

# 9. Testing
## 9.1 Unit tests
## 9.2 Dev tests
## 9.3 System tests

# 10. Documentation Impact

# 11. References
