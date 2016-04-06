contrail-db-loader
==================
Simple CLI to generate Contrail config data in a Cassandra database.

## Installation

### Python virtualenv

You can install ``contrail-db-loader`` inside a python virtualenv.
First create the virtualenv and install ``contrail-db-loader`` from source.

    $ git clone https://github.com/Juniper/contrail-controller.git
    $ cd contrail-controller/src/config/utils/db-loader
    $ virtualenv --system-site-packages contrail-db-loader-venv
    $ source contrail-db-loader-venv/bin/activate
    (contrail-db-loader-venv) $ python setup.py install

Created virtual environment have access to the global site-packages where it should find the cfgm_common module. That ugly hack because it's hard/impossible to install cfgm_common module in the virtual environment (generated code...).
TODO: Remove dependency with cfgm_common module. Write the own Cassandra db interface. Could be faster because we can use shortcuts for that use case.

## Usage
In that sample, the script creates 60 projects with 40 security groups, 200 virtual networks, and 200 virtual machine interfaces:
    $ cat resources.yaml
    project: 60
    security-group: 40
    virtual-network: 200
    virtual-machine-interface: 200
The VNC config API server have to be started at least one time before we can start that script because it uses default resources created by the server API when it initialized.
    (contrail-db-loader-venv) $ source ~/devstack/openrc admin admin
    (contrail-db-loader-venv) $ contrail-db-loader --os-auth-plugin v2password --resources-file resources.yaml
    INFO:requests.packages.urllib3.connectionpool:Starting new HTTP connection (1): localhost
    WARNING:contrail_db_loader.main:Will populate 60 projects with:
        - security groups:           41
        - access control lists:      82
        - virtual networks:          200
        - routing instances:         200
        - route targets:             200
        - virtual machine interface: 200
        - virtual machine:           200
        - intance ip:                200
    That will load 79440 resources into database.
    Do you want to load that amount of resources? [y/n]: y
    INFO:contrail_db_loader.main:Loading 'project' resources into the database...
    INFO:requests.packages.urllib3.connectionpool:Starting new HTTP connection (1): localhost
    INFO:contrail_db_loader.main:60 resources were created to load 60 'project' in 1.30 seconds.
    INFO:contrail_db_loader.main:Loading 'security-group' resources into the database...
    INFO:contrail_db_loader.main:7380 resources were created to load 40 'security-group' in 7.41 seconds.
    INFO:contrail_db_loader.main:Loading 'virtual-network' resources into the database...
    INFO:contrail_db_loader.main:36000 resources were created to load 200 'virtual-network' in 49.66 seconds.
    INFO:contrail_db_loader.main:Loading 'virtual-machine-interface' resources into the database...
    INFO:contrail_db_loader.main:36000 resources were created to load 200 'virtual-machine-interface' in 65.88 seconds.

    real    2m5.872s
    user    1m17.188s
    sys     0m6.232s

## Authentication

``contrail-db-loader`` supports Keystone (v2, v3) to synchronize projects between Contrail database and Keystone.

### Basic HTTP auth

    contrail-db-loader --os-auth-plugin http --os-username admin --os-password contrail123 ...

The username and password can be sourced from the environment variables ``OS_USERNAME``, ``OS_PASSWORD``.

The auth plugin default to ``http`` unless ``OS_AUTH_PLUGIN`` is set.

### Keystone v2 or v3 API auth

The easiest way is to source your openstack openrc file and run

    contrail-db-loader --os-auth-plugin [v2password|v3password] shell

See ``contrail-db-loader --os-auth-plugin [v2password|v3password] --help`` for all options.
