#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#
# MESOS CNI data handling
#


class MESOSCniDataObject:

    def __init__(self, data=None):
        self._data = data
        self._conf = {}

    def parse_cni_data(self):
        data = self._data
        self._conf['cid'] = data['cid']
        self._conf['cmd'] = data['cmd']
        lbl_dict = {}
        lbl_dict['cluster-name'] = data['contrail']['cluster-name']
        net_info = data['args']['org.apache.mesos']['network_info']
        if net_info and 'labels' in net_info:
            cni_labels = net_info['labels']
            if cni_labels:
                for item in cni_labels['labels']:
                    lbl_dict[item['key']] = item['value']
                self._conf['labels'] = lbl_dict

        return self._conf
