package command_test

import (
	"testing"
	"strings"
	"fmt"
	"net/http"
	"net/http/httptest"
	"vcenter-import/command"
	"vcenter-import/vcenter"
)

func TestTokens(t *testing.T) {
	type commandConfig struct {
		authHost string
		authUsername string
		authPassword string
		authClusterID string
		authClusterToken string
	}
	validCfg1 := &commandConfig{
		authUsername: "admin",
		authPassword: "password",
	}
	validCfg2 := &commandConfig{
		authClusterID: "cluster4",
		authClusterToken: "IAmAValidToken",
	}
	inValidCfg := &commandConfig{
		authHost: "2.2.2.22.2:33454",
		authUsername: "admin",
		authPassword: "password",
	}
	tests := []struct {
		desc string
		statusHeader  int
		setHeader bool
		commandCfg *commandConfig
		wantErr bool
	} {{
		desc: "test-0",
		statusHeader: http.StatusUnauthorized,
		commandCfg: &commandConfig{},
		wantErr: true,
	}, {
		desc: "test-1",
		statusHeader: http.StatusUnauthorized,
		commandCfg: validCfg2,
		wantErr: true,
	}, {
		desc: "test-2",
		statusHeader: http.StatusUnauthorized,
		commandCfg: validCfg1,
		wantErr: true,
	}, {
		desc: "test-3",
		statusHeader: http.StatusOK,
		setHeader: true,
		commandCfg: validCfg2,
	}, {
		desc: "test-4",
		statusHeader: http.StatusOK,
		setHeader: true,
		commandCfg: validCfg2,
	}, {
		desc: "test-5",
		statusHeader: http.StatusOK,
		setHeader: true,
		commandCfg: inValidCfg,
		wantErr: true,
	}}

	for _, tt := range tests {
		t.Run(tt.desc, func (t *testing.T) {
			ts := httptest.NewTLSServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
				if tt.setHeader {
					w.Header().Set("X-Subject-Token", "1234567890abcdefghidflflf")
				}
				w.WriteHeader(tt.statusHeader)
				fmt.Fprintln(w, "Hello, client")
			}))
			defer ts.Close()
			cmdURL := strings.TrimPrefix(ts.URL, "https://")
			ccClient := command.New(tt.commandCfg.authHost,
						tt.commandCfg.authUsername,
						tt.commandCfg.authPassword,
						tt.commandCfg.authClusterID,
						tt.commandCfg.authClusterToken)
			if tt.commandCfg.authHost == "" {
				ccClient.AuthHost = cmdURL
			}
			if err := ccClient.Token(); (err != nil) != tt.wantErr {
				t.Fatalf("Token() failed: %s", tt.desc )
			}
		})
	}
}

func TestNode(t *testing.T) {
	type commandConfig struct {
		authHost string
		authUsername string
		authPassword string
		authClusterID string
		authClusterToken string
	}
	validCfg1 := &commandConfig{
		authUsername: "admin",
		authPassword: "password",
	}
	inValidCfg := &commandConfig{
		authHost: "2.2.2.22.2:33454",
		authUsername: "admin",
		authPassword: "password",
	}
	tests := []struct {
		desc string
		statusHeader  int
		respBody []byte
		commandCfg *commandConfig
		wantErr bool
	} {{
		desc: "test-1",
		statusHeader: http.StatusUnauthorized,
		commandCfg: validCfg1,
		respBody : []byte(`{}`),
		wantErr: true,
	}, {
		desc: "test-3",
		statusHeader: http.StatusOK,
		commandCfg: validCfg1,
		respBody : []byte(``),
		wantErr: true,
	}, {
		desc: "test-4",
		statusHeader: http.StatusOK,
		commandCfg: validCfg1,
		respBody : []byte(`{ { "uuid": "0505c414-78ab-47ad-90db-b4c1b5ea3c66", "name": "ddd", "parent_uuid": "beefbeef-beef-beef-beef-beefbeef0001", "parent_type": "global-system-config", "fq_name": [ "default-global-system-config", "ddd" ], "display_name": "ddd", "hostname": "ddd", "interface_name": "eth0", "ip_address": "1.1.1.1", "type": "private", "node_type": "private", "to": [ "default-global-system-config", "ddd" ] } ] }`),
		wantErr: true,
	}, {
		desc: "test-5",
		statusHeader: http.StatusOK,
		commandCfg: inValidCfg,
		wantErr: true,
	}, {
		desc: "test-6",
		statusHeader: http.StatusOK,
		commandCfg: validCfg1,
		respBody : []byte(`{ "nodes": [ { "node": { "uuid": "0505c414-78ab-47ad-90db-b4c1b5ea3c66", "name": "ddd", "parent_uuid": "beefbeef-beef-beef-beef-beefbeef0001", "parent_type": "global-system-config", "fq_name": [ "default-global-system-config", "ddd" ], "display_name": "ddd", "type": "private", "node_type": "private", "ports": [ { "uuid": "180cbbbb-7ca6-4e01-b984-58ce7e037d7d", "name": "eth0", "parent_uuid": "0505c414-78ab-47ad-90db-b4c1b5ea3c66", "parent_type": "node", "fq_name": [ "default-global-system-config", "ddd", "eth0" ], "display_name": "eth0", "ip_address": "1.1.1.1", "bms_port_info": { "local_link_connection": {} }, "esxi_port_info": {}, "to": [ "default-global-system-config", "ddd", "eth0" ] } ], "to": [ "default-global-system-config", "ddd" ] } }, { "node": { "uuid": "425074ec-e4a9-4513-8aa3-1bf65c06bb9b", "name": "5b11s1-node1-vm2", "parent_uuid": "beefbeef-beef-beef-beef-beefbeef0001", "parent_type": "global-system-config", "fq_name": [ "default-global-system-config", "5b11s1-node1-vm2" ], "display_name": "5b11s1-node1-vm2", "hostname": "5b11s1-node1-vm2", "type": "private", "node_type": "private", "to": [ "default-global-system-config", "5b11s1-node1-vm2" ] } } ] }`),
	}}

	for _, tt := range tests {
		t.Run(tt.desc, func (t *testing.T) {
			ts := httptest.NewTLSServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
				w.WriteHeader(tt.statusHeader)
				if len(tt.respBody) > 0 {
					fmt.Fprintln(w, string(tt.respBody))
				}
			}))
			defer ts.Close()
			cmdURL := strings.TrimPrefix(ts.URL, "https://")
			ccClient := command.New(tt.commandCfg.authHost,
						tt.commandCfg.authUsername,
						tt.commandCfg.authPassword,
						tt.commandCfg.authClusterID,
						tt.commandCfg.authClusterToken)
			if tt.commandCfg.authHost == "" {
				ccClient.AuthHost = cmdURL
			}
			if _, err := ccClient.Nodes(); (err != nil) != tt.wantErr {
				t.Fatalf("test Node() failed: %s", tt.desc )
			}
		})
	}
}

// node-create failed
func TestCreateNode1(t *testing.T) {
	ts := httptest.NewTLSServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.WriteHeader(http.StatusConflict)
		fmt.Fprintln(w, "Hello, client")
	}))
	defer ts.Close()
	cmdURL := strings.TrimPrefix(ts.URL, "https://")
	ccClient := command.New(cmdURL,"user", "password", "", "")
	h := vcenter.ESXIHost {
		Hostname: "t1",
		UUID:"this-is-valid-uuid",
		Ports: nil,
	}
	err := ccClient.CreateNode(h)
	if  err == nil {
		t.Errorf("test failed: error not nil")
	}
}
//node-create passed
func TestCreateNode2(t *testing.T) {
	ts := httptest.NewTLSServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.WriteHeader(http.StatusOK)
		fmt.Fprintln(w, "Hello, client")
	}))
	defer ts.Close()
	cmdURL := strings.TrimPrefix(ts.URL, "https://")
	ccClient := command.New(cmdURL,"user", "password", "", "")
	h := vcenter.ESXIHost {
		Hostname: "t2",
		UUID:"this-is-valid-uuid",
		Ports: nil,
	}
	err := ccClient.CreateNode(h)
	if  err != nil {
		t.Errorf("test failed: error not nil")
	}
}

func TestCreatePort1(t *testing.T) {
	ts := httptest.NewTLSServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.WriteHeader(http.StatusConflict)
		fmt.Fprintln(w, "Hello, client")
	}))
	defer ts.Close()
	cmdURL := strings.TrimPrefix(ts.URL, "https://")
	ccClient := command.New(cmdURL,"user", "password", "", "")
	h := vcenter.ESXIHost {
		Hostname: "t1",
		UUID:"this-is-valid-uuid",
		Ports: nil,
	}
	p := vcenter.ESXIPort {
		Name: "p1",
		MACAddress: "ab:cd:ef:ab:cd:ef",
		SwitchName:"",
		PortIndex: "",
		ChassisID: "",
		PortID: "",
		DVSName: "",
		UUID: "",
	}
	err := ccClient.CreatePort(p, h)
	if  err == nil {
		t.Errorf("test failed: error not nil")
	}
}
func TestCreatePort2(t *testing.T) {
	ts := httptest.NewTLSServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.WriteHeader(http.StatusOK)
		fmt.Fprintln(w, "Hello, client")
	}))
	defer ts.Close()
	cmdURL := strings.TrimPrefix(ts.URL, "https://")
	ccClient := command.New(cmdURL,"user", "password", "", "")
	h := vcenter.ESXIHost {
		Hostname: "t2",
		UUID:"this-is-valid-uuid",
		Ports: nil,
	}
	p := vcenter.ESXIPort {
		Name: "p1",
		MACAddress: "ab:cd:ef:ab:cd:ef",
		SwitchName:"",
		PortIndex: "",
		ChassisID: "",
		PortID: "",
		DVSName: "",
		UUID: "",
	}
	err := ccClient.CreatePort(p, h)
	if  err != nil {
		t.Errorf("test failed: error not nil")
	}
}
