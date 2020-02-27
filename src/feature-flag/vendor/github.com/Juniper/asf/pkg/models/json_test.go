package models

import (
	"encoding/json"
	"testing"

	"github.com/stretchr/testify/assert"
)

func TestJSONMarshal(t *testing.T) {
	tests := []struct {
		name     string
		input    interface{}
		expected string
	}{
		{
			name:     "omits nil fields of generated types",
			input:    &VirtualNetwork{},
			expected: `{}`,
		},
		{
			name:     "does not omit nil fields of KeyValuePair object",
			input:    &KeyValuePair{},
			expected: `{"value":"","key":""}`,
		},
		{
			name:     "does not omit nil value field of KeyValuePair object",
			input:    &KeyValuePair{Key: "hoge"},
			expected: `{"value":"","key":"hoge"}`,
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			o, err := json.Marshal(tt.input)

			assert.NoError(t, err)
			assert.Equal(t, tt.expected, string(o))
		})
	}
}

func TestJSONUnmarshal(t *testing.T) {
	tests := []struct {
		name            string
		data            string
		input, expected interface{}
		fails           bool
	}{
		{name: "zero", fails: true},
		{name: "empty json object", data: `{}`, fails: true},
		{
			name:     "empty object",
			input:    &VirtualMachineInterface{},
			data:     `{}`,
			expected: &VirtualMachineInterface{},
		},
		{
			name:     "simple data",
			input:    &VirtualMachineInterface{},
			data:     `{"uuid": "foo", "configuration_version": 1337}`,
			expected: &VirtualMachineInterface{UUID: "foo", ConfigurationVersion: 1337},
		},
		{
			name:     "integer as string",
			input:    &VirtualMachineInterface{},
			data:     `{"configuration_version": "1337"}`,
			expected: &VirtualMachineInterface{ConfigurationVersion: 1337},
		},
		{
			name:     "port as integer",
			input:    &BgpRouterParams{},
			data:     `{"port": 2778}`,
			expected: &BgpRouterParams{Port: 2778},
		},
		{
			name:     "port as string",
			input:    &BgpRouterParams{},
			data:     `{"port": "2778"}`,
			expected: &BgpRouterParams{Port: 2778},
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			err := json.Unmarshal([]byte(tt.data), tt.input)
			if tt.fails {
				assert.Error(t, err)
			} else {
				assert.NoError(t, err)
			}
			assert.Equal(t, tt.expected, tt.input)
		})
	}
}

// Below benchmark compares simple struct unmarshaling to map unmarshaling
// with ApplyMap. CPU performance looks similar, but allocation load is in favor
// of simple option.
//
// $ go test -bench Unmarshal -benchtime 30s -benchmem
// goos: linux
// goarch: amd64
// pkg: github.com/Juniper/asf/pkg/models
// BenchmarkJSONUnmarshal-4          	  500000	    111058 ns/op	    6352 B/op	      79 allocs/op
// BenchmarkUnmarshalAndApplyMap-4   	  500000	    112913 ns/op	   21353 B/op	     479 allocs/op

func BenchmarkJSONUnmarshal(b *testing.B) {
	for i := 0; i < b.N; i++ {
		if err := json.Unmarshal([]byte(testResource), &testVN); err != nil {
			b.Fatal(err)
		}
	}
}

func BenchmarkUnmarshalAndApplyMap(b *testing.B) {
	var m map[string]interface{}
	for i := 0; i < b.N; i++ {
		if err := json.Unmarshal([]byte(testResource), &m); err != nil {
			b.Fatal(err)
		}
		if err := testVN.ApplyMap(m); err != nil {
			b.Fatal(err)
		}
	}
}

var testVN VirtualNetwork

const testResource string = `{
  "uuid": "e533e5eb-5b92-450e-bb0a-2ca512f05c98",
  "name": "",
  "parent_uuid": "bd216d57-bec0-4527-bdf6-2970c5541735",
  "parent_type": "project",
  "fq_name": [
    "default-domain",
    "k8s-default",
    "k8s-default-service-network"
  ],
  "id_perms": {
    "enable": true,
    "description": "",
    "created": "2018-08-22T19:16:15.015477",
    "creator": "",
    "user_visible": true,
    "last_modified": "2018-08-24T17:25:35.736133",
    "permissions": {
      "owner": "cloud-admin",
      "owner_access": 7,
      "other_access": 7,
      "group": "cloud-admin-group",
      "group_access": 7
    },
    "uuid": {
      "uuid_mslong": 16515797057514128654,
      "uuid_lslong": 13477633922358598808
    }
  },
  "display_name": "k8s-default-service-network",
  "annotations": null,
  "perms2": {
    "owner": "None",
    "owner_access": 7,
    "global_access": 0,
    "share": []
  },
  "configuration_version": 0,
  "address_allocation_mode": "flat-subnet-only",
  "ecmp_hashing_include_fields": null,
  "export_route_target_list": null,
  "external_ipam": false,
  "fabric_snat": false,
  "flood_unknown_unicast": false,
  "igmp_enable": false,
  "import_route_target_list": null,
  "is_provider_network": false,
  "is_shared": false,
  "layer2_control_word": false,
  "mac_aging_time": 0,
  "mac_learning_enabled": false,
  "mac_limit_control": null,
  "mac_move_control": null,
  "multi_policy_service_chains_enabled": false,
  "pbb_etree_enable": false,
  "pbb_evpn_enable": false,
  "port_security_enabled": false,
  "provider_properties": null,
  "route_target_list": null,
  "router_external": false,
  "virtual_network_fat_flow_protocols": null,
  "virtual_network_network_id": 5,
  "virtual_network_properties": {
    "allow_transit": false,
    "forwarding_mode": "l3",
    "max_flow_rate": 0,
    "max_flows": 0,
    "mirror_destination": false,
    "network_id": 0,
    "rpf": "",
    "vxlan_network_identifier": 0
  },
  "multicast_policy_refs": [],
  "network_policy_refs": [
    {
      "uuid": "135e6212-adca-442c-a25a-b9a980ca5e4a",
      "to": [],
      "attr": {
        "sequence": {
          "major": 0,
          "minor": 0
        },
        "timer": null
      }
    },
    {
      "uuid": "1f7799ea-3817-4456-b3ca-41e393b31cd3",
      "to": [],
      "attr": {
        "sequence": {
          "major": 0,
          "minor": 0
        },
        "timer": null
      }
    },
    {
      "uuid": "f63aa250-e686-4f74-80ec-ccc0fb42fd4b",
      "to": [],
      "attr": {
        "sequence": {
          "major": 0,
          "minor": 0
        },
        "timer": null
      }
    }
  ],
  "virtual_network_refs": [],
  "bgpvpn_refs": [],
  "logical_router_refs": [],
  "network_ipam_refs": [
    {
      "uuid": "3c9e49b2-a011-4a31-a8cc-1bb885a76336",
      "to": [],
      "attr": {
        "host_routes": null,
        "ipam_subnets": [
          {
            "addr_from_start": false,
            "alloc_unit": 0,
            "allocation_pools": [],
            "created": "",
            "default_gateway": "",
            "dhcp_option_list": null,
            "dns_nameservers": [],
            "dns_server_address": "",
            "enable_dhcp": false,
            "host_routes": null,
            "last_modified": "",
            "subnet": null,
            "subnet_name": "",
            "subnet_uuid": "90b85ea3-1b2d-4ff2-b2ab-1443f5879137",
            "subscriber_tag": ""
          }
        ]
      }
    }
  ],
  "qos_config_refs": [],
  "route_table_refs": [],
  "routing_policy_refs": [],
  "security_logging_object_refs": [],
  "physical_router_back_refs": [],
  "virtual_machine_interface_back_refs": [],
  "virtual_network_back_refs": [],
  "fabric_back_refs": [],
  "firewall_rule_back_refs": [],
  "instance_ip_back_refs": [],
  "logical_router_back_refs": [],
  "access_control_lists": [],
  "alias_ip_pools": [],
  "bridge_domains": [],
  "floating_ip_pools": [],
  "routing_instances": []
}`
