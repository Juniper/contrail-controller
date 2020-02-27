package models

import (
	"testing"

	"github.com/stretchr/testify/assert"
)

func TestInterfaceToVirtualMachineInterface(t *testing.T) {
	tests := []struct {
		name  string
		input interface{}
		want  *VirtualMachineInterface
	}{
		{name: "nil"},
		{name: "empty map", input: map[string]interface{}{}, want: &VirtualMachineInterface{}},
		{
			name: "simple props",
			input: map[string]interface{}{
				"uuid": "some-uuid",
				"name": "some-name",
			},
			want: &VirtualMachineInterface{UUID: "some-uuid", Name: "some-name"},
		},
		{
			name: "annotations provided as kv list",
			input: map[string]interface{}{
				"annotations": []interface{}{
					map[string]interface{}{"key": "k", "value": "v"},
				},
			},
			want: &VirtualMachineInterface{
				Annotations: &KeyValuePairs{KeyValuePair: []*KeyValuePair{{Value: "v", Key: "k"}}},
			},
		},
		{
			name: "annotations with subfield",
			input: map[string]interface{}{
				"annotations": map[string]interface{}{"key_value_pair": []interface{}{
					map[string]interface{}{"key": "k", "value": "v"},
				}},
			},
			want: &VirtualMachineInterface{
				Annotations: &KeyValuePairs{KeyValuePair: []*KeyValuePair{{Value: "v", Key: "k"}}},
			},
		},
		{
			name: "annotations as object",
			input: map[string]interface{}{
				"annotations": &KeyValuePairs{KeyValuePair: []*KeyValuePair{{Value: "v", Key: "k"}}},
			},
			want: &VirtualMachineInterface{
				Annotations: &KeyValuePairs{KeyValuePair: []*KeyValuePair{{Value: "v", Key: "k"}}},
			},
		},
		{
			name: "fat flow protocols provided as list of objects",
			input: map[string]interface{}{
				"virtual_machine_interface_fat_flow_protocols": []interface{}{
					map[string]interface{}{"ignore_address": "none", "port": 0, "protocol": "proto"},
				},
			},
			want: &VirtualMachineInterface{
				VirtualMachineInterfaceFatFlowProtocols: &FatFlowProtocols{FatFlowProtocol: []*ProtocolType{
					{IgnoreAddress: "none", Port: 0, Protocol: "proto"},
				}},
			},
		},
		{
			name: "fat flow protocols with subfield",
			input: map[string]interface{}{
				"virtual_machine_interface_fat_flow_protocols": map[string]interface{}{
					"fat_flow_protocol": []interface{}{
						map[string]interface{}{"ignore_address": "none", "port": 0, "protocol": "proto"},
					},
				},
			},
			want: &VirtualMachineInterface{
				VirtualMachineInterfaceFatFlowProtocols: &FatFlowProtocols{FatFlowProtocol: []*ProtocolType{
					{IgnoreAddress: "none", Port: 0, Protocol: "proto"},
				}},
			},
		},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			got := InterfaceToVirtualMachineInterface(tt.input)
			assert.Equal(t, tt.want, got)
		})
	}
}

func TestVirtualMachineInterfaceApplyMap(t *testing.T) {
	tests := []struct {
		name  string
		obj   *VirtualMachineInterface
		input map[string]interface{}
		want  *VirtualMachineInterface
	}{
		{name: "nil"},
		{name: "nil obj", input: map[string]interface{}{"uuid": "value"}},
		{name: "nil map", obj: &VirtualMachineInterface{}, want: &VirtualMachineInterface{}},
		{
			name:  "empty map",
			obj:   &VirtualMachineInterface{},
			input: map[string]interface{}{},
			want:  &VirtualMachineInterface{},
		},
		{
			name: "simple props",
			obj:  &VirtualMachineInterface{UUID: "old-uuid", Name: "some-name"},
			input: map[string]interface{}{
				"uuid": "some-uuid",
			},
			want: &VirtualMachineInterface{UUID: "some-uuid", Name: "some-name"},
		},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			err := tt.obj.ApplyMap(tt.input)
			assert.NoError(t, err)
			assert.Equal(t, tt.want, tt.obj)
		})
	}
}

func TestVirtualMachineInterface_GetMacAddressesType(t *testing.T) {
	tests := []struct {
		name                                string
		UUID                                string
		VirtualMachineInterfaceMacAddresses *MacAddressesType
		want                                *MacAddressesType
		fails                               bool
	}{
		{name: "empty", fails: true},
		{
			name:                                "mac in mac addresses type with colons",
			VirtualMachineInterfaceMacAddresses: &MacAddressesType{MacAddress: []string{"be:ef:be:ef:be:ef"}},
			want:                                &MacAddressesType{MacAddress: []string{"be:ef:be:ef:be:ef"}},
		},
		{
			name:                                "mac in mac addresses type with dashes",
			VirtualMachineInterfaceMacAddresses: &MacAddressesType{MacAddress: []string{"be-ef-be-ef-be-ef"}},
			want:                                &MacAddressesType{MacAddress: []string{"be:ef:be:ef:be:ef"}},
		},
		{
			name: "mac from uuid",
			UUID: "01234567890",
			want: &MacAddressesType{MacAddress: []string{"02:01:23:45:67:90"}},
		},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			m := &VirtualMachineInterface{
				UUID:                                tt.UUID,
				VirtualMachineInterfaceMacAddresses: tt.VirtualMachineInterfaceMacAddresses,
			}
			got, err := m.GetMacAddressesType()

			if tt.fails {
				assert.Error(t, err)
			} else {
				assert.NoError(t, err)
				assert.Equal(t, tt.want, got)
			}
		})
	}
}
