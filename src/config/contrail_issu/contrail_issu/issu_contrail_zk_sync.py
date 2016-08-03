#!/usr/bin/python
#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#

import time
import logging
import logging.handlers
import issu_contrail_config

from pysandesh.gen_py.sandesh.ttypes import SandeshLevel
from cfgm_common.zkclient import ZookeeperClient


class ContrailZKIssu():

    def __init__(self, Old_Version_Address, New_Version_Address,
                 Old_Prefix, New_Prefix, Znode_Issu_List, logger):
        self._Old_ZK_Version_Address = Old_Version_Address
        self._New_ZK_Version_Address = New_Version_Address
        self._Old_Prefix = '/' + Old_Prefix
        self._New_Prefix = '/' + New_Prefix
        self._Znode_Issu_List = list(Znode_Issu_List)
        self._logger = logger
        self._logger(
            "Issu contrail zookeeper initialized...",
            level=SandeshLevel.SYS_INFO,
        )
 
    # end __init__

    # Create new path recursively
    def _zk_copy(self, old_v_path, new_v_path):
        children = self._zk_old.get_children(old_v_path)
        value = self._zk_old.read_node(old_v_path)
        self._logger(
            "Issu contrail zookeeper, _zk_copy, old version path"
            + str(old_v_path), level=SandeshLevel.SYS_DEBUG,
        )
        self._logger(
            "Issu contrail zookeeper, _zk_copy, new version path"
            + str(new_v_path), level=SandeshLevel.SYS_DEBUG,
        )
        self._zk_new.create_node(new_v_path, value)
        value = self._zk_new.read_node(new_v_path)
        self._logger(
            "Issu contrail zookeeper ,_zk_copy, new value"
            + str(value), level=SandeshLevel.SYS_DEBUG,
        )

        for _path in children:
            new_path = str(new_v_path) + '/' + str(_path)
            old_path = str(old_v_path) + '/' + str(_path)
            self._zk_copy(old_path, new_path)
    # end _zk_copy

    def issu_compare(self, new_prefix, old_prefix):
        for _path in self._Znode_Issu_List:
            new_path = new_prefix + str(_path)
            old_path = old_prefix + str(_path)
            _new_children = self._zk_new.get_children(new_path)
            _old_children = self._zk_old.get_children(old_path)
            _new_children.sort()
            _old_children.sort()
            _result = cmp(_new_children, _old_children)
            if (_result == 0):
                continue
            else:
                self._logger(
                    "Issu contrail zookeeper failed...",
                    level=SandeshLevel.SYS_ERROR,
                )
                break
        self._logger(
            "Issu contrail zookeeper passed...",
            level=SandeshLevel.SYS_INFO,
        )
    # end issu_compare

    def issu_zk_start(self):
        # Connect to old and new ZK servers
        self._zk_old = ZookeeperClient("zk issu client older version",
                                       self._Old_ZK_Version_Address)
        self._zk_old.set_lost_cb(self.issu_restart)
        self._zk_old.set_suspend_cb(self.issu_restart)

        self._zk_new = ZookeeperClient("zk issu client newer version",
                                       self._New_ZK_Version_Address)
        self._zk_new.set_lost_cb(self.issu_restart)
        self._zk_new.set_suspend_cb(self.issu_restart)

        old_prefix = self._Old_Prefix + "/"
        new_prefix = self._New_Prefix + "/"

        # Delete all state in new ZK if any

        if self._zk_new.exists(new_prefix):
            children = self._zk_new.get_children(new_prefix)
            for _path in children:
                if _path == "zookeeper":
                    continue
                self._logger(
                    "Issu contrail zookeeper ,issu_zk_start, deleted paths"
                    + str((new_prefix + str(_path))),
                    level=SandeshLevel.SYS_INFO,
                )
                self._zk_new.delete_node((new_prefix + str(_path)), True)
        else:
            self._zk_new.create_node(new_prefix, "")

        if self._zk_old.exists(old_prefix):
            children = self._zk_old.get_children(old_prefix)

        for _path in children:
            # Ignore zookeeper replication
            if _path in self._Znode_Issu_List: 
                new_path = new_prefix + str(_path)
                old_path = old_prefix + str(_path)
                time.sleep(1)
                self._zk_copy(old_path, new_path)
            else:
                continue

        self.issu_compare(new_prefix, old_prefix)
    # end issu_zk_start

    def issu_restart(self):
        # Call the ISSU start when connection to zk is lost in middle of ISSU
        self._logger(
            "Issu contrail zookeeper restarted...",
            level=SandeshLevel.SYS_INFO,
        )
        # drop the zookeeper connection
        self._zk_old._zk_client.stop()
        self._zk_new._zk_client.stop()

        # Call the ISSU start again.
        self.issu_zk_start()
    # end issu_restart

# end ContrailZKIssu

def _issu_zk_main():

    logging.basicConfig(
        level=logging.INFO,
        filename='/var/log/issu_contrail_zk.log',
        format='%(asctime)s %(message)s')

    args, remaining_args = issu_contrail_config.parse_args()
    issu_handle = ContrailZKIssu(args.old_zookeeper_address_list,
                                 args.new_zookeeper_address_list,
                                 args.odb_prefix,
                                 args.ndb_prefix,
                                 issu_contrail_config.issu_znode_list,
                                 issu_contrail_config.logger)
    issu_handle.issu_zk_start()

if __name__ == "__main__":
    _issu_zk_main()
