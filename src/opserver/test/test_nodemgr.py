#!/usr/bin/env python
#
#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
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
from gevent.subprocess import Popen, PIPE
from supervisor import xmlrpc
import nodemgr
import nodemgr.common.event_manager
import nodemgr.control_nodemgr.control_event_manager

logging.basicConfig(level=logging.INFO,
                    format='%(asctime)s %(levelname)s %(message)s')

class NodemgrTest(unittest.TestCase):

    @mock.patch('os.path.getmtime')
    @mock.patch('glob.glob')
    @mock.patch('nodemgr.common.event_manager.Popen')
    @mock.patch('os.remove')
    @mock.patch('__builtin__.open')
    @mock.patch('nodemgr.control_nodemgr.control_event_manager.ControlEventManager.send_process_state_db')
    @mock.patch('nodemgr.control_nodemgr.control_event_manager.ControlEventManager.add_current_process')
    @mock.patch('nodemgr.control_nodemgr.control_event_manager.ControlEventManager.send_system_cpu_info')
    def test_nodemgr(self, mock_send_system_cpu_info, mock_add_current_process,
        mock_send_process_state_db, mock_open, mock_remove, mock_popen, mock_glob, mock_tm_time):
        headers = {}
        headers['expected']='0'
        headers['pid']='123'
        cm = nodemgr.control_nodemgr.control_event_manager.\
            ControlEventManager('','','','')
        proc_stat = nodemgr.common.process_stat.ProcessStat('proc1')
        # create 4 core files
        cm.process_state_db['proc1'] = proc_stat
        mock_popen.return_value.returncode = 0
        mock_popen.return_value.communicate.return_value = ('core.proc1.1', '')
        cm.send_process_state('proc1', 'PROCESS_STATE_EXITED',headers)
        mock_popen.return_value.communicate.return_value = ('core.proc1.2', '')
        cm.send_process_state('proc1', 'PROCESS_STATE_EXITED',headers)
        mock_popen.return_value.communicate.return_value = ('core.proc1.3', '')
        cm.send_process_state('proc1', 'PROCESS_STATE_EXITED',headers)
        mock_popen.return_value.communicate.return_value = ('core.proc1.4', '')
        cm.send_process_state('proc1', 'PROCESS_STATE_EXITED',headers)
        self.assertEqual(len(cm.process_state_db['proc1'].core_file_list), 4)
        # add the 5th core file
        mock_popen.return_value.communicate.return_value = ('core.proc1.5', '')
        cm.send_process_state('proc1', 'PROCESS_STATE_EXITED',headers)
        # test manual deletion of core file
        #mock_popen.return_value = ('', 0)
        mock_glob.return_value = ['core.proc1.1','core.proc1.2', 'core.proc1.3', 'core.proc1.4', 'core.proc1.5']
        #mock_sort.return_value = ['core.proc1.2', 'core.proc1.3', 'core.proc1.4']
        def mock_sort(i,j):
            return 1
        mock_tm_time = mock_sort
        status = cm.update_process_core_file_list()
        exp_core_list = ['core.proc1.2', 'core.proc1.3', 'core.proc1.4', 'core.proc1.5']
        # there should be no core files
        self.assertEqual(len(cm.process_state_db['proc1'].core_file_list), 4)
        self.assertEqual(cm.process_state_db['proc1'].core_file_list, exp_core_list)
        # Calls with less core files should not change the file
        status = cm.update_process_core_file_list()
        self.assertEqual(len(cm.process_state_db['proc1'].core_file_list), 4)

if __name__ == '__main__':
    unittest.main()
