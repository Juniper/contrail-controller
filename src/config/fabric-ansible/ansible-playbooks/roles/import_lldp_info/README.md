Role Name
=========

This role is for topology_discovery. It mainly aims at discovering topology between physical-interfaces. This role may be tested with or without device discovery or device import. But for complete results, it is best to test this role after device has been discovered and on-boarded. This role is idempotent, meaning, the topology will be discovered with the devices at hand, should more devices be discovered, the topology will be updated.

Requirements
------------

Any pre-requisites that may not be covered by Ansible itself or the role should be mentioned here. For instance, if the role uses the EC2 module, it may be a good idea to mention in this section that the boto package is required.

Role Variables
--------------

A description of the settable variables for this role should go here, including any variables that are in defaults/main.yml, vars/main.yml, and any variables that can/should be set via parameters to the role. Any variables that are read from other roles and/or the global scope (ie. hostvars, group vars, etc.) should be mentioned here as well.

Dependencies
------------

A list of other roles hosted on Galaxy should go here, plus any details in regards to parameters that may need to be set for other roles, or variables that are used from other roles.

Example Playbook
----------------
The main Playbook is topology_discovery.yml. It includes the topology_discovery role. This is a multi-device playbook and can be invoked for multiple devices at once from the job_manager.



License
-------

http://www.apache.org/licenses/LICENSE-2.0

Author Information
------------------

An optional section for the role authors to include contact information, or a website (HTML is not allowed).
