#!/usr/bin/env bash
set -x
function issu_contrail_set_supervisord_config_files {
    local cmd="openstack-config --set /etc/contrail/supervisord_config_files/$1.ini program:$1"
    $cmd autostart $2
    $cmd autorestart $2
    $cmd killasgroup $2
}

function issu_contrail_prepare_new_control_node {
    contrail-status
    val="$(lsb_release -sr | cut -d \. -f 1)"
    if [ $val == 14 ]
    then
        issu_contrail_set_supervisord_config_files 'contrail-device-manager' 'false'
        issu_contrail_set_supervisord_config_files 'contrail-svc-monitor' 'false'
        issu_contrail_set_supervisord_config_files 'contrail-schema' 'false'

        openstack-config --set /etc/contrail/supervisord_config_files/contrail-config-nodemgr.ini eventlistener:contrail-config-nodemgr autorestart false
        openstack-config --set /etc/contrail/supervisord_config_files/contrail-config-nodemgr.ini eventlistener:contrail-config-nodemgr autostart false

        service contrail-config-nodemgr stop
        service contrail-control stop
        service contrail-schema stop
        service contrail-svc-monitor stop
        service contrail-device-manager stop
    fi
    if [ $val == 16 ]
    then
        systemctl stop contrail-config-nodemgr
        systemctl stop contrail-control
        systemctl stop contrail-schema
        systemctl stop contrail-svc-monitor
        systemctl stop contrail-device-manager
        systemctl disable contrail-config-nodemgr
        systemctl disable contrail-svc-monitor
	systemctl disable contrail-device-manager
	systemctl disable contrail-schema
    fi
    contrail-status
}

function issu_contrail_post_new_control_node {
    contrail-status
    val="$(lsb_release -sr | cut -d \. -f 1)"
    if [ $val == 14 ]
    then
            #openstack-config --set /etc/contrail/supervisord_config.conf include files \"/etc/contrail/supervisord_config_files/*.ini\"

    issu_contrail_set_supervisord_config_files 'contrail-device-manager' 'true'
    issu_contrail_set_supervisord_config_files 'contrail-svc-monitor' 'true'
    issu_contrail_set_supervisord_config_files 'contrail-schema' 'true'

    openstack-config --del /etc/contrail/supervisord_config_files/contrail-config-nodemgr.ini eventlistener:contrail-config-nodemgr autorestart
    openstack-config --del /etc/contrail/supervisord_config_files/contrail-config-nodemgr.ini eventlistener:contrail-config-nodemgr autostart
    service contrail-config-nodemgr start
    service contrail-control start
    service contrail-schema start
    service contrail-svc-monitor start
    service contrail-device-manager start
    fi
    if [ $val == 16 ]
    then
        systemctl enable contrail-config-nodemgr
        systemctl enable contrail-svc-monitor
        systemctl enable contrail-device-manager
	systemctl enable contrail-schema
        systemctl start contrail-config-nodemgr
        systemctl start contrail-control
        systemctl start contrail-schema
        systemctl start contrail-svc-monitor
        systemctl start contrail-device-manager
    fi
    contrail-status

}

function issu_pre_sync {
    contrail-issu-pre-sync -c /etc/contrail/contrail-issu.conf
}

function issu_run_sync {
    val="$(lsb_release -sr | cut -d \. -f 1)"
    if [ $val == 14 ]
    then
        supervisorctl restart supervisord_issu
    fi
    if [ $val == 16 ]
    then
        systemctl enable contrail-issu-run-sync
        systemctl restart contrail-issu-run-sync
    fi
}

function issu_post_sync {
    val="$(lsb_release -sr | cut -d \. -f 1)"
    if [ $val == 14 ]
    then
        supervisorctl stop supervisord_issu
    fi
    if [ $val == 16 ]
    then
        systemctl stop contrail-issu-run-sync
        systemctl disable contrail-issu-run-sync
    fi
    contrail-issu-post-sync -c /etc/contrail/contrail-issu.conf
    contrail-issu-zk-sync -c /etc/contrail/contrail-issu.conf
}

function issu_contrail_generate_conf {
    local myfile="/tmp/contrail-issu.conf"
    issu_contrail_get_and_set_old_conf $1 $myfile
    issu_contrail_get_and_set_new_conf $2 $myfile
    echo $1 $2
}

function issu_contrail_get_and_set_old_conf {
    local get_old_cmd="openstack-config --get  $1  DEFAULTS"
    local has_old_cmd="openstack-config --has $1 DEFAULTS"
    local set_cmd="openstack-config --set $2 DEFAULTS"

    cmd="$get_old_cmd cassandra_server_list"
    val=$($cmd)
    $set_cmd   old_cassandra_address_list "$val"

    cmd="$get_old_cmd zk_server_ip"
    val=$($cmd)
    $set_cmd old_zookeeper_address_list "$val"

    cmd="$has_old_cmd rabbit_user"
    val=$($cmd)
    if [ $val == 1 ]
    then
        cmd="$get_old_cmd rabbit_user"
        val=$($cmd)
        $set_cmd old_rabbit_user "$val"
    fi

    cmd="$has_old_cmd rabbit_password"
    val=$($cmd)
    if [ $val == 1 ]
    then
        cmd="$get_old_cmd rabbit_password"
        val=$($cmd)
        $set_cmd old_rabbit_password "$val"
    fi

    cmd="$has_old_cmd rabbit_vhost"
    val=$($cmd)
    if [ $val == 1 ]
    then
        cmd="$get_old_cmd rabbit_vhost"
        val=$($cmd)
        $set_cmd old_rabbit_vhost "$val"
    fi

    cmd="$has_old_cmd rabbit_ha_mode"
    val=$($cmd)
    if [ $val == 1 ]
    then
        cmd="$get_old_cmd rabbit_ha_mode"
        val=$($cmd)
        $set_cmd old_rabbit_ha_mode "$val"
    fi

    cmd="$has_old_cmd rabbit_port"
    val=$($cmd)
    if [ $val == 1 ]
    then
        cmd="$get_old_cmd rabbit_port"
        val=$($cmd)
        $set_cmd old_rabbit_port "$val"
    fi

    cmd="$has_old_cmd rabbit_server"
    val=$($cmd)
    if [ $val == 1 ]
    then
        cmd="$get_old_cmd rabbit_server"
        val=$($cmd)
        $set_cmd old_rabbit_address_list "$val"
    fi

    cmd="$has_old_cmd cluster_id"
    val=$($cmd)
    if [ $val == 1 ]
    then
        cmd="$get_old_cmd cluster_id"
        val=$($cmd)
        $set_cmd odb_prefix "$val"
    fi


}

function issu_contrail_get_and_set_new_conf {
    local get_new_cmd="openstack-config --get $1 DEFAULTS"
    local set_cmd="openstack-config --set $2 DEFAULTS"
    local has_new_cmd="openstack-config --has $1 DEFAULTS"

    cmd="$get_new_cmd cassandra_server_list"
    val=$($cmd)
    $set_cmd new_cassandra_address_list "$val"

    cmd="$get_new_cmd zk_server_ip"
    val=$($cmd)
    $set_cmd new_zookeeper_address_list "$val"

    cmd="$has_new_cmd rabbit_user"
    val=$($cmd)
    if [ $val == 1 ]
    then
        cmd="$get_new_cmd rabbit_user"
        val=$($cmd)
        $set_cmd new_rabbit_user "$val"
    fi

    cmd="$has_new_cmd rabbit_password"
    val=$($cmd)
    if [ $val == 1 ]
    then
        cmd="$get_new_cmd rabbit_password"
        val=$($cmd)
        $set_cmd new_rabbit_password "$val"
    fi

    cmd="$has_new_cmd rabbit_vhost"
    val=$($cmd)
    if [ $val == 1 ]
    then
        cmd="$get_new_cmd rabbit_vhost"
        val=$($cmd)
        $set_cmd new_rabbit_vhost "$val"
    fi

    cmd="$has_new_cmd rabbit_ha_mode"
    val=$($cmd)
    if [ $val == 1 ]
    then
        cmd="$get_new_cmd rabbit_ha_mode"
        val=$($cmd)
        $set_cmd new_rabbit_ha_mode "$val"
    fi

    cmd="$has_new_cmd rabbit_port"
    val=$($cmd)
    if [ $val == 1 ]
    then
        cmd="$get_new_cmd rabbit_port"
        val=$($cmd)
        $set_cmd new_rabbit_port "$val"
    fi

    cmd="$has_new_cmd rabbit_server"
    val=$($cmd)
    if [ $val == 1 ]
    then
        cmd="$get_new_cmd rabbit_server"
        val=$($cmd)
        $set_cmd new_rabbit_address_list "$val"
    fi

    cmd="$has_new_cmd cluster_id"
    val=$($cmd)
    if [ $val == 1 ]
    then
        cmd="$get_new_cmd cluster_id"
        val=$($cmd)
        $set_cmd ndb_prefix "$val"
    fi
}

function issu_contrail_peer_control_nodes {
    python /opt/contrail/utils/provision_pre_issu.py --conf /etc/contrail/contrail-issu.conf
}

function issu_contrail_finalize_config {
    python /opt/contrail/utils/provision_issu.py --conf /etc/contrail/contrail-issu.conf
}

function issu_contrail_fetch_api_conf {
    cp /etc/contrail/contrail-api.conf /etc/contrailctl/contrail-api.conf
}

function issu_contrail_set_conf {
    cp /etc/contrailctl/contrail-issu.conf /etc/contrail/contrail-issu.conf
}

function myfunc {
    echo "Hello World $1"
}

ARGC=$#
if [ $ARGC == 0 ]
then
    echo "Usage: $0 <function name> <arguments>"
    exit;
fi

case $1 in
    myfunc)
      if [ $ARGC == 2 ]
      then
        $1 $2
        exit
      fi
      echo "Usage: $0 $1 <arguments>"
      ;;
    issu_contrail_generate_conf)
      if [ $ARGC == 2 ]
      then
        $1 $2 $3
        exit
      fi
      echo "Usage: $0 $1 <arguments>"
      ;;
    issu_contrail_prepare_new_control_node)
      if [ $ARGC == 1 ]
      then
        $1
        exit
      fi
      echo "Usage: $0 $1 "
      ;;
    issu_contrail_post_new_control_node)
      if [ $ARGC == 1 ]
      then
        $1
        exit
      fi
      echo "Usage: $0 $1 "
      ;;
    issu_pre_sync)
      if [ $ARGC == 1 ]
      then
        $1
        exit
      fi
      echo "Usage: $0 $1 "
      ;;
    issu_run_sync)
      if [ $ARGC == 1 ]
      then
        $1
        exit
      fi
      echo "Usage: $0 $1 "
      ;;
    issu_post_sync)
      if [ $ARGC == 1 ]
      then
        $1
        exit
      fi
      echo "Usage: $0 $1 "
      ;;
    issu_contrail_finalize_config)
      if [ $ARGC == 1 ]
      then
        $1
        exit
      fi
      echo "Usage: $0 $1 "
      ;;
    issu_contrail_fetch_api_conf)
      if [ $ARGC == 1 ]
      then
        $1
        exit
      fi
      echo "Usage: $0 $1 "
      ;;
    issu_contrail_set_conf)
      if [ $ARGC == 1 ]
      then
        $1
        exit
      fi
      echo "Usage: $0 $1 "
      ;;
    issu_contrail_peer_control_nodes)
      if [ $ARGC == 1 ]
      then
        $1
        exit
      fi
      echo "Usage: $0 $1 "
      ;;

    *)
      echo -e "Unrecognized function $1"
      ;;
esac
