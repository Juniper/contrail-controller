#!/usr/bin/python


class FilterModule(object):
    def filters(self):
        return {
            'parse_neighbor_info': self.lldp_neighbormap_filter,
            'get_bulk_ref_add_payload': self.bulk_ref_payload_filter,
            'get_port_id_fqname_map': self.get_port_id_fqname_mapping,
        }

    def lldp_neighbormap_filter(self, lldp_neighbor_info, prouter_fqname):
        """This filter takes inputs as the local prouter_fqname
        and the output of the
        show lldp neighbors command (lldp_neighbor_info).
        The lldp_neighbor_info can be
        of the following two formats:

        Format A:
        "lldp_neighbor_info": [
                {
                    "lldp-local-parent-interface-name": "-",
                    "lldp-local-port-id": "ge-1/3/7",
                    "lldp-remote-chassis-id": "00:26:88:db:3f:c0",
                    "lldp-remote-chassis-id-subtype": "Mac address",
                    "lldp-remote-port-id": "588",
                    "lldp-remote-port-id-subtype": "Locally assigned",
                    "lldp-remote-system-name": "jtme-mx240-03"
                },
                {
                    "lldp-local-parent-interface-name": "-",
                    "lldp-local-port-id": "fxp0",
                    "lldp-remote-chassis-id": "40:b4:f0:7d:1f:00",
                    "lldp-remote-chassis-id-subtype": "Mac address",
                    "lldp-remote-port-id": "850",
                    "lldp-remote-port-id-subtype": "Locally assigned",
                    "lldp-remote-system-name": "nsmsw2"
                }
            ]

        Format B:
        "lldp_neighbor_info": [
                {
                    "lldp-local-parent-interface-name": "-",
                    "lldp-local-port-id": "xe-0/0/47",
                    "lldp-remote-chassis-id": "84:b5:9c:c8:00:00",
                    "lldp-remote-chassis-id-subtype": "Mac address",
                    "lldp-remote-port-description": "xe-0/0/45",
                    "lldp-remote-system-name": "5b11-qfx1"
                },
                {
                    "lldp-local-parent-interface-name": "-",
                    "lldp-local-port-id": "em0",
                    "lldp-remote-chassis-id": "84:b5:9c:c8:00:00",
                    "lldp-remote-chassis-id-subtype": "Mac address",
                    "lldp-remote-port-description": "ge-0/0/38",
                    "lldp-remote-system-name": "5b11-qfx1"
                }]

        If the lldp_neighbor_info is of format A, then the filter returns:

        {
                  "do_more_parsing": True,
                  "remote_neighbors_list":[remote_prouter_fqname1,
                                           remote_prouter_fqname2 ...],
                  "neighbor_map_info_list": [{
                                              "local_phy_int_fqname":[],
                                              "remote_phy_int_port_id":
                                                  <remote_prouter_name:int-val>
                                             },
                                             {
                                              "local_phy_int_fqname":[],
                                              "remote_phy_int_port_id":
                                                  <remote_prouter_name:int-val>
                                             },
                                             {
                                              "local_phy_int_fqname":[],
                                              "remote_phy_int_port_id":
                                                  <remote_prouter_name:int-val>
                                             }
                                            ]
        }

        else returns this format:
        {
                  "do_more_parsing": False,
                  "remote_neighbors_list":[remote_prouter_fqname1,
                                           remote_prouter_fqname2 ...],
                  "neighbor_map_info_list": [
                                              [<local_phy_int_fqname>,
                                              <remote_phy_int_fqname>],
                                              [<local_phy_int_fqname>,
                                              <remote_phy_int_fqname>],
                                              [<local_phy_int_fqname>,
                                              <remote_phy_int_fqname>]
                                            ]
        }

        The field: neighbor_map_info maintains the mapping of the
        local_interface_fqname to the remote_interface_info
        and the field: remote_neigbors_list will maintain
        the set of all the remote neighbors of the current local prouter.

        The remote_neighbors_list field is required to get the
        physical-interfaces on the corresponding (remote neighboring)
        prouters from a bulk query to vnc-db if lldp_neighbor_info is
        of format A.

        """
        remote_neighbors_list = []
        neighbor_map_info_list = []
        err_msg_list = []

        if isinstance(lldp_neighbor_info, dict):
            lldp_neighbor_info = [lldp_neighbor_info]

        needs_parsing = True
        # pilot sequence to determine what the structure of
        # each lldp_neighbor_info is like
        if 'lldp-remote-port-description' in lldp_neighbor_info[0]:
            needs_parsing = False

        for lldp_neighbor in lldp_neighbor_info:
            try:
                remote_prouter_fqname = ["default-global-system-config",
                                         lldp_neighbor.get(
                                             'lldp-remote-system-name')]
                if lldp_neighbor.get('lldp-remote-system-name') is None:
                    continue

                phy_int_fqname = []
                phy_int_fqname.extend(prouter_fqname)
                local_phy_int = lldp_neighbor.get('lldp-local-port-id') \
                    or lldp_neighbor.get('lldp-local-interface')
                phy_int_fqname.append(local_phy_int.replace(":", "_"))

                if needs_parsing:
                    # ensure local interface is not a logical interface,
                    # remote interface need not be checked as port no
                    # won't be imported
                    if '.' not in phy_int_fqname[-1]:
                        neighbor_map_info_list.append({
                                                "local_phy_int_fqname":
                                                phy_int_fqname,
                                                "remote_phy_int_port_id":
                                                lldp_neighbor.get(
                                                    'lldp-remote-system-name') +
                                                ":" + lldp_neighbor.get(
                                                    'lldp-remote-port-id')
                                              })
                else:
                    lldp_remote_neighbor_port_desc = lldp_neighbor.get(
                        'lldp-remote-port-description').split(
                        "interface")[-1].strip()
                    lldp_neighbor_fqname = []
                    lldp_neighbor_fqname.extend(remote_prouter_fqname)
                    lldp_neighbor_fqname.append(
                        lldp_remote_neighbor_port_desc.replace(":", "_"))
                    # ensure local interface and remote interfaces are
                    # not logical interfaces
                    if '.' not in phy_int_fqname[-1]\
                            and '.' not in lldp_neighbor_fqname[-1]:
                        neighbor_pair = (phy_int_fqname, lldp_neighbor_fqname)
                        neighbor_map_info_list.append(neighbor_pair)

                remote_neighbors_list.append(remote_prouter_fqname)
            except Exception as ex:
                err_msg_list.append(str(ex))

        remote_neighbors_set = set(map(tuple, remote_neighbors_list))
        return {"remote_neighbors_list": list(remote_neighbors_set),
                "neighbor_map_info_list": neighbor_map_info_list,
                "do_more_parsing": needs_parsing,
                "err_msg_list": err_msg_list}

    def get_port_id_fqname_mapping(self, bulk_query_prouter_resp):
        """This filter takes input from the vnc_db_mod bulk_query response
        and parses it to give a port-id:fqname mapping for all
        physical interfaces of the neighboring routers.

        returns: {"<prouter_name>": {"<port-id1>": [<fq_name>],
                                     "<port-id2>": [<fq_name>]},
                  "<prouter_name>": {"<port-id1>": [<fq_name>],
                                     "<port-id2>": [<fq_name>]}
                 }

        """
        all_remote_phy_intfs = bulk_query_prouter_resp['list_objects']
        port_id_fqname_prouter_map = dict()
        for remote_phy_intfs in all_remote_phy_intfs:
            port_id_fqname_map = dict()
            phy_intfs = remote_phy_intfs['obj']['physical-interfaces']
            for phy_intf in phy_intfs:
                port_id = phy_intf.get('physical_interface_port_id')
                port_id_fqname_map[port_id] = phy_intf['fq_name']
            if port_id_fqname_map:
                prouter_name = phy_intfs[0]['fq_name'][-2]
                port_id_fqname_prouter_map[prouter_name] = port_id_fqname_map
        return port_id_fqname_prouter_map

    def bulk_ref_payload_filter(self, port_id_fqname_prouter_map,
                                neighbor_map_info_list):
        """This filter takes inputs as the port_id_fqname_prouter_map
        and neighbor_map_info_list and returns the payload for
        bulk-ref-update (in vnc-module).

        The port_id_fqname_prouter_map is of the format:

        {
         "<prouter_name>": {"<port-id1>": [<fq_name>],
                            "<port-id2>": [<fq_name>]},
         "<prouter_name>": {"<port-id1>": [<fq_name>],
                            "<port-id2>": [<fq_name>]}
        }

        The neighbor_map_info_list will be of the format:
        "neighbor_map_info_list": [{
                                     "local_phy_int_fqname":[],
                                     "remote_phy_int_port_id":
                                         <remote_prouter_name:int-val>
                                   },
                                   {
                                     "local_phy_int_fqname":[],
                                     "remote_phy_int_port_id":
                                         <remote_prouter_name:int-val>
                                   },
                                   {
                                     "local_phy_int_fqname":[],
                                     "remote_phy_int_port_id":
                                         <remote_prouter_name:int-val>
                                   }]

        The payload is a list of tuples that has the local_phy_int_fqname
        and the remote_phy_int_fqname (fq_name has been chosen to maintain
        uniformity).

        returns: [(<local_phy_int_fqname>, <remote_phy_int_fqname>),
                   (<local_phy_int_fqname>, <remote_phy_int_fqname>),
                   (<local_phy_int_fqname>, <remote_phy_int_fqname>)
                 ]

        """

        bulk_ref_payload_list = []

        for lldp_neighbor in neighbor_map_info_list:
            lldp_neighbor_port_info = lldp_neighbor.get(
                'remote_phy_int_port_id').rsplit(":", 1)
            lldp_neighbor_prouter_name = lldp_neighbor_port_info[0]
            lldp_neighbor_port = lldp_neighbor_port_info[1]

            port_id_fqname_map = port_id_fqname_prouter_map.get(
                lldp_neighbor_prouter_name)
            if port_id_fqname_map:
                lldp_neighbor_fqname = port_id_fqname_map.get(
                    lldp_neighbor_port)

                if lldp_neighbor_fqname:
                    neighbor_pair = (lldp_neighbor.get(
                        'local_phy_int_fqname'),
                                     lldp_neighbor_fqname)
                    bulk_ref_payload_list.append(neighbor_pair)

        return bulk_ref_payload_list
