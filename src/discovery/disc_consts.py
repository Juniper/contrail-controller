#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

_WEB_HOST = '127.0.0.1'
_WEB_PORT = 5998
_CASSANDRA_HOST = '127.0.0.1'
_CASSANDRA_PORT = 9160
_CASSANDRA_MAX_RETRIES = 5
_CASSANDRA_TIMEOUT = 0.5
_ZK_HOST = '127.0.0.1'
_ZK_PORT = 2181
_TTL_MIN = 1 * 60
_TTL_MAX = 3 * 60
CLIENT_TAG = '$client-entry$'
SERVICE_TAG = 'service-entry'
ADMIN_STATE_TAG = 'admin-state'
MAX_COL = 10000000
DEFAULT_VERSION='1.0'

# keep subscription around for a short while to allow client to renew
TTL_EXPIRY_DELTA = 30

# Health check ping interval
HC_INTERVAL = 5

# Expire published info after successive heartbeat miss
HC_MAX_MISS = 5

