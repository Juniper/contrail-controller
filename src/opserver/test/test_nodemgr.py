#!/usr/bin/env python

#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

#
# NodemgrTest
#
# Unit Tests for testing nodemgr 
#

import logging
import os
import sys
import copy
import mock
import unittest
import pdb
import json
from subprocess import Popen, PIPE
from supervisor import xmlrpc
from opserver.uveserver import UVEServer
from opserver.uveserver import ParallelAggregator
from opserver.opserver_util import OpServerUtils
import nodemgr
import nodemgr.common.event_manager
import nodemgr.control_nodemgr.control_event_manager

logging.basicConfig(level=logging.INFO,
                    format='%(asctime)s %(levelname)s %(message)s')

class NodemgrTest(unittest.TestCase):

    @mock.patch('subprocess.Popen.communicate')
    @mock.patch('os.remove')
    @mock.patch('__builtin__.open')
    @mock.patch('nodemgr.control_nodemgr.control_event_manager.ControlEventManager.send_process_state_db')
    @mock.patch('nodemgr.control_nodemgr.control_event_manager.ControlEventManager.add_current_process')
    @mock.patch('nodemgr.control_nodemgr.control_event_manager.ControlEventManager.send_system_cpu_info')
    def test_nodemgr(self, mock_send_system_cpu_info, mock_add_current_process,
        mock_send_process_state_db, mock_open, mock_remove, mock_popen):
        headers = {}
        headers['expected']='0'
        headers['pid']='123'
        cm = nodemgr.control_nodemgr.control_event_manager.\
            ControlEventManager('','','','')
        proc_stat = nodemgr.common.process_stat.ProcessStat('proc1')
        # create 4 core files
        cm.process_state_db['proc1'] = proc_stat
        mock_popen.return_value = ('core.proc1.1', 0)
        cm.send_process_state('proc1', 'PROCESS_STATE_EXITED',headers)
        mock_popen.return_value = ('core.proc1.2', 0)
        cm.send_process_state('proc1', 'PROCESS_STATE_EXITED',headers)
        mock_popen.return_value = ('core.proc1.3', 0)
        cm.send_process_state('proc1', 'PROCESS_STATE_EXITED',headers)
        mock_popen.return_value = ('core.proc1.4', 0)
        cm.send_process_state('proc1', 'PROCESS_STATE_EXITED',headers)
        self.assertEqual(len(cm.process_state_db['proc1'].core_file_list), 4)
        # add the 5th core file
        mock_popen.return_value = ('core.proc1.5', 0)
        cm.send_process_state('proc1', 'PROCESS_STATE_EXITED',headers)
        # only 4 core files should be listed
        self.assertEqual(len(cm.process_state_db['proc1'].core_file_list), 4)
        # core file1 should have been deleted
        self.assertEqual('core.proc1.1' in set(cm.process_state_db['proc1'].\
            core_file_list), False)
        # test manual deletion of core file
        mock_popen.return_value = ('', 0)
        status = cm.update_process_core_file_list()
        # there should be no core files
        self.assertEqual(len(cm.process_state_db['proc1'].core_file_list), 0)

if __name__ == '__main__':
    unittest.main()
