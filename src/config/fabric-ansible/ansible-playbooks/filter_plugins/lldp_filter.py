#!/usr/bin/python

class FilterModule(object):
    def filters(self):
        return {
            'parse_neighbor_info': self.lldp_neighbormap_filter,
            'get_bulk_ref_add_payload': self.bulk_ref_payload_filter,
            'get_port_id_fqname_map': self.get_port_id_fqname_mapping,
        }

    '''

    This filter takes inputs as the local prouter_fqname and the output of the
    show lldp neighbors command and returns 2 fields. The field: neighbor_map_info
    maintains the mapping of the local_interface_fqname to the remote_interface_info
    and the field: remote_neigbors_list will maintain the set of all the remote
    neighbors of the current local prouter.

    The remote_neighbors_list field is required to get the physical-interfaces on the
    corresponding (remote neighboring) prouters from a bulk query to vnc-db

    returns: {
              "remote_neighbors_list":[remote_prouter_fqname1, remote_prouter_fqname2 ...],
              "neighbor_map_info_list": [{
                                          "local_phy_int_fqname":[],
                                          "remote_phy_int_port_id": <remote_prouter_name:int-val>
                                         },
                                         {
                                          "local_phy_int_fqname":[],
                                          "remote_phy_int_port_id": <remote_prouter_name:int-val>
                                         },
                                         {
                                          "local_phy_int_fqname":[],
                                          "remote_phy_int_port_id": <remote_prouter_name:int-val>
                                         }
                                        ]
             }
    '''
    def lldp_neighbormap_filter(self, lldp_neighbor_info, prouter_fqname):

        remote_neighbors_list = []
        neighbor_map_info_list = []

        if isinstance(lldp_neighbor_info, dict):
            lldp_neighbor_info = [lldp_neighbor_info]

        for lldp_neighbor in lldp_neighbor_info:

            remote_prouter_fqname = ["default-global-system-config", lldp_neighbor.get('lldp-remote-system-name')]

            phy_int_fqname = []
            phy_int_fqname.extend(prouter_fqname)
            phy_int_fqname.append(lldp_neighbor.get('lldp-local-port-id'))

            neighbor_map_info_list.append({
                                            "local_phy_int_fqname": phy_int_fqname,
                                            "remote_phy_int_port_id": lldp_neighbor.get('lldp-remote-system-name') + ":" + lldp_neighbor.get('lldp-remote-port-id')
                                          })
            remote_neighbors_list.append(remote_prouter_fqname)
        remote_neighbors_set = set(map(tuple, remote_neighbors_list))
        return {"remote_neighbors_list": list(remote_neighbors_set), "neighbor_map_info_list": neighbor_map_info_list}

    '''

    This filter takes input from the vnc_db_mod bulk_query response and parses it to give a port-id:fqname
    mapping for all physical interfaces of the neighboring routers.

    returns: {"<prouter_name>": {"<port-id1>": [<fq_name>], "<port-id2>": [<fq_name>]},
              "<prouter_name>": {"<port-id1>": [<fq_name>], "<port-id2>": [<fq_name>]}
             }

    '''
    def get_port_id_fqname_mapping(self, bulk_query_prouter_resp):
        all_remote_phy_intfs = bulk_query_prouter_resp['list_objects']
        port_id_fqname_prouter_map = dict()
        for remote_phy_intfs in all_remote_phy_intfs:
            port_id_fqname_map = dict()
            phy_intfs = remote_phy_intfs['obj']['physical-interfaces']
            for phy_intf in phy_intfs:
                port_id = phy_intf.get('port_id')
                port_id_fqname_map [port_id]=  phy_intf['fq_name']
            if port_id_fqname_map:
                prouter_name = phy_intfs[0]['fq_name'][-2]
                port_id_fqname_prouter_map [prouter_name] = port_id_fqname_map
        return port_id_fqname_prouter_map

    '''

    This filter takes inputs as the port_id_fqname_map and neighbor_map_info_list
    and returns the payload for bulk-ref-update (in vnc-module). The payload is a list of
    tuples that has the local_phy_int_fqname and the remote_phy_int_fqname

    (fq_name has been chosen to maintain uniformity)

    returns: [(<local_phy_int_fqname>, <remote_phy_int_fqname>),
               (<local_phy_int_fqname>, <remote_phy_int_fqname>),
               (<local_phy_int_fqname>, <remote_phy_int_fqname>)
             ]

    '''
    def bulk_ref_payload_filter(self, port_id_fqname_prouter_map, neighbor_map_info_list):

        bulk_ref_payload_list = []

        for lldp_neighbor in neighbor_map_info_list:
            lldp_neighbor_port_info = lldp_neighbor.get('remote_phy_int_port_id').rsplit(":", 1)
            lldp_neighbor_prouter_name = lldp_neighbor_port_info[0]
            lldp_neighbor_port = lldp_neighbor_port_info[1]

            port_id_fqname_map = port_id_fqname_prouter_map.get(lldp_neighbor_prouter_name)
            if port_id_fqname_map:
                lldp_neighbor_fqname = port_id_fqname_map.get(lldp_neighbor_port)

                if lldp_neighbor_fqname:
                    neighbor_pair = (lldp_neighbor.get('local_phy_int_fqname'), lldp_neighbor_fqname)
                    bulk_ref_payload_list.append(neighbor_pair)

        return bulk_ref_payload_list
