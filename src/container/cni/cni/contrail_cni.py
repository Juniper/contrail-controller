#!/usr/bin/python
# vim: tabstop=4 shiftwidth=4 softtabstop=4

#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#

"""
Contrail CNI plugin
"""
from __future__ import absolute_import
from future import standard_library
standard_library.install_aliases()
import configparser

from .mesos_cni import contrail_mesos_cni
from .kube_cni import contrail_kube_cni

ORCHES_FILE = '/etc/contrail/orchestrator.ini'

ORCHES_MESOS = 'mesos'
ORCHES_K8S   = 'kubernetes'


def main():
    try:
        config = configparser.ConfigParser(interpolation=None)
        config.read(ORCHES_FILE)
        orches = config.get('DEFAULT', 'orchestrator')
    except configparser.Error:
        orches = ORCHES_MESOS

    if orches == ORCHES_K8S:
        contrail_kube_cni.main()
    else:
        contrail_mesos_cni.main()

if __name__ == "__main__":
    main()
