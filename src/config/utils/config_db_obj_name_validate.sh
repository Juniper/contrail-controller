#!/bin/sh

# example invocation is
# cd /opt/contrail/utils && DB_HOST=<db-node-ip> ./config_db_obj_name_validate.sh

DB_HOST=${DB_HOST:-127.0.0.1}
pycassaShell -H ${DB_HOST} -k config_db_uuid -f ./config_db_obj_name_validate.py
