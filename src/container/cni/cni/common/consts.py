# vim: tabstop=4 shiftwidth=4 softtabstop=4
#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#

# Error codes from params module
PARAMS_ERR_ENV = 101
PARAMS_ERR_DOCKER_CONNECTION = 102
PARAMS_ERR_GET_UUID = 103

# Default VRouter related values
VROUTER_AGENT_IP = '127.0.0.1'
VROUTER_AGENT_PORT = 9091
VROUTER_POLL_TIMEOUT = 3
VROUTER_POLL_RETRIES = 20

# Container mode. Can only be k8s or mesos
CONTRAIL_CONTAINER_MODE = "k8s"
CONTRAIL_CONTAINER_MTU = 1500
CONTRAIL_CONFIG_DIR = '/var/lib/contrail/ports/vm'

# Default K8S Pod related values
POD_DEFAULT_MTU = 1500

# Logging parameters
LOG_FILE = '/var/log/contrail/cni/opencontrail.log'
LOG_LEVEL = 'WARNING'

