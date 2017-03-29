#!/usr/bin/env bash

function issu_contrail_switch_compute_node {
    route -n
    openstack-config --set /etc/contrail/contrail-vrouter-agent.conf DISCOVERY server $1 
    openstack-config --set /etc/contrail/supervisord_vrouter_files/contrail-vrouter-agent.ini program:contrail-vrouter-agent autostart true
    openstack-config --set /etc/contrail/supervisord_vrouter_files/contrail-vrouter-agent.ini program:contrail-vrouter-agent killasgroup true
    openstack-config --set /etc/contrail/contrail-vrouter-nodemgr.conf DISCOVERY server $1 
    for i in `ls /etc/contrail/contrail-tor-agent-*`; do echo $i; openstack-config --set $i DISCOVERY server $1; done
    service supervisor-vrouter stop; rmmod vrouter;modprobe vrouter;service supervisor-vrouter start
    contrail-status
    route -n
}

function issu_contrail_prepare_compute_node {
            route -n
            openstack-config --del /etc/contrail/supervisord_vrouter_files/contrail-vrouter-agent.ini program:contrail-vrouter-agent autostart
            openstack-config --del /etc/contrail/supervisord_vrouter_files/contrail-vrouter-agent.ini program:contrail-vrouter-agent killasgroup
            contrail-status
}

function issu_contrail_set_supervisord_config_files {
    local cmd="openstack-config --set /etc/contrail/supervisord_config_files/$1.ini program:$1"
    $cmd autostart $2 
    $cmd autorestart $2 
    $cmd killasgroup $2
}

function issu_contrail_prepare_new_control_node {
    contrail-status
    issu_contrail_set_supervisord_config_files 'contrail-device-manager' 'false'
    issu_contrail_set_supervisord_config_files 'contrail-svc-monitor' 'false'
    issu_contrail_set_supervisord_config_files 'contrail-schema' 'false'

    openstack-config --set /etc/contrail/supervisord_config_files/contrail-config-nodemgr.ini eventlistener:contrail-config-nodemgr autorestart false
    openstack-config --set /etc/contrail/supervisord_config_files/contrail-config-nodemgr.ini eventlistener:contrail-config-nodemgr autostart false

    contrail-status
    service supervisor-config restart
    contrail-status
    service supervisor-config stop
    contrail-status
}

function issu_contrail_post_new_control_node {
    contrail-status
            #openstack-config --set /etc/contrail/supervisord_config.conf include files \"/etc/contrail/supervisord_config_files/*.ini\"
            
    issu_contrail_set_supervisord_config_files 'contrail-device-manager' 'true'
    issu_contrail_set_supervisord_config_files 'contrail-svc-monitor' 'true'
    issu_contrail_set_supervisord_config_files 'contrail-schema' 'true'

    openstack-config --del /etc/contrail/supervisord_config_files/contrail-config-nodemgr.ini eventlistener:contrail-config-nodemgr autorestart
    openstack-config --del /etc/contrail/supervisord_config_files/contrail-config-nodemgr.ini eventlistener:contrail-config-nodemgr autostart

    service supervisor-config restart
    contrail-status

}

function issu_pre_sync {
    contrail-issu-pre-sync -c /etc/contrail/contrail-issu.conf
}

function issu_run_sync {
    local cmd='openstack-config --set /etc/supervisor/conf.d/contrail-issu.conf program:contrail-issu'
    touch /etc/supervisor/conf.d/contrail-issu.conf
    $cmd command 'contrail-issu-run-sync --conf_file /etc/contrail/contrail-issu.conf'
    $cmd numprocs 1
    openstack-config --set /etc/supervisor/conf.d/contrail-issu.conf program:contrail-issu process_name '%(process_num)s'
    $cmd redirect_stderr true
    openstack-config --set /etc/supervisor/conf.d/contrail-issu.conf program:contrail-issu stdout_logfile  '/var/log/issu-contrail-run-sync-%(process_num)s-stdout.log'
    openstack-config --set /etc/supervisor/conf.d/contrail-issu.conf program:contrail-issu stderr_logfile '/dev/null' 
    $cmd priority 440
    $cmd autostart true
    $cmd killasgroup false
    $cmd stopsignal KILL
    $cmd exitcodes 0
    service supervisor restart
}

function issu_post_sync {
    rm -f /etc/supervisor/conf.d/contrail-issu.conf
    service supervisor restart
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
