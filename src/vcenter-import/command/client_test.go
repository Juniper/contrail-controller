package command_test

import (
	"testing"
	"strings"
	"fmt"
	"net/http"
	"net/http/httptest"
	"vcenter-import/command"
)


func TesttokenT1( t *testing.T) {
	ccClient := command.New("","", "", "", "")
	err := ccClient.Token()
	t.Errorf("test failed")
	if err != nil {
		t.Errorf("test failed")
	}
}
func startTestServer() *httptest.Server {
	ts := httptest.NewTLSServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("X-Subject-Token", "1234567890abcdefghidflflf")
		fmt.Fprintln(w, "Hello, client")
	}))
	return ts
}
func TestToken2(t *testing.T) {
	ts := httptest.NewTLSServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("X-Subject-Token", "1234567890abcdefghidflflf")
		fmt.Fprintln(w, "Hello, client")
	}))
	defer ts.Close()
	cmdURL := strings.TrimPrefix(ts.URL, "https://")
	fmt.Println(ts.URL, cmdURL)
	ccClient := command.New(cmdURL,"user", "password", "", "")
	err := ccClient.Token()
	if err != nil {
		fmt.Println(err)
		t.Errorf("test failed: error not nil")
	}
	if ccClient.AuthToken == "" {
		t.Errorf("test failed: auth token empty")
	}
}
func TestToken3(t *testing.T) {
	ts := httptest.NewTLSServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		fmt.Fprintln(w, "Hello, client")
	}))
	defer ts.Close()
	cmdURL := strings.TrimPrefix(ts.URL, "https://")

	ccClient := command.New(cmdURL,"user", "password", "", "")
	err := ccClient.Token()

	if err == nil {
		fmt.Println(err)
		t.Errorf("test failed: error not nil")
	}
	if ccClient.AuthToken != "" {
		t.Errorf("test failed: auth token empty")
	}
}
func TestToken4(t *testing.T) {
	ts := httptest.NewTLSServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.WriteHeader(http.StatusInternalServerError)
		fmt.Fprintln(w, "Hello, client")
	}))
	defer ts.Close()
	cmdURL := strings.TrimPrefix(ts.URL, "https://")

	ccClient := command.New(cmdURL,"user", "password", "", "")
	err := ccClient.Token()

	if err == nil {
		fmt.Println(err)
		t.Errorf("test failed: error not nil")
	}
	if ccClient.AuthToken != "" {
		t.Errorf("test failed: auth token empty")
	}
}
func TestToken5(t *testing.T) {
	ts := httptest.NewTLSServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.WriteHeader(http.StatusInternalServerError)
		fmt.Fprintln(w, "Hello, client")
	}))
	defer ts.Close()
	cmdURL := strings.TrimPrefix(ts.URL, "https://")

	ccClient := command.New(cmdURL,"user", "password", "cluster4", "IamAClusterToken")
	err := ccClient.Token()

	if err == nil {
		fmt.Println(err)
		t.Errorf("test failed: error not nil")
	}
	if ccClient.AuthToken != "" {
		t.Errorf("test failed: auth token empty")
	}
}
func TestToken6(t *testing.T) {
	ccClient := command.New("2.2.2.22.2:33454","user", "password", "cluster4", "IamAClusterToken")
	err := ccClient.Token()

	if err == nil {
		fmt.Println(err)
		t.Errorf("test failed: error not nil")
	}
	if ccClient.AuthToken != "" {
		t.Errorf("test failed: auth token empty")
	}
}
func TestNodes1(t *testing.T) {
	ts := httptest.NewTLSServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.WriteHeader(http.StatusOK)
		fmt.Fprintln(w, "{}")
	}))
	defer ts.Close()
	cmdURL := strings.TrimPrefix(ts.URL, "https://")

	ccClient := command.New(cmdURL,"user", "password", "cluster4", "IamAClusterToken")
	ccClient.AuthToken = "ThisIsValidToken"
	nodes, err := ccClient.Nodes()
	if err != nil {
		fmt.Println(err)
		t.Errorf("test failed: error not nil")
	}
	if len(nodes) != 0 {
		t.Errorf("test failed: node not nil")
	}
}
func TestNodes2(t *testing.T) {
	ts := httptest.NewTLSServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.WriteHeader(http.StatusUnauthorized)
		fmt.Fprintln(w, "{}")
	}))
	defer ts.Close()
	cmdURL := strings.TrimPrefix(ts.URL, "https://")

	ccClient := command.New(cmdURL,"user", "password", "cluster4", "IamAClusterToken")
	ccClient.AuthToken = "ThisIsInValidToken"
	nodes, err := ccClient.Nodes()
	if err != nil {
		fmt.Println(err)
		t.Errorf("test failed: error not nil")
	}
	if len(nodes) != 0 {
		t.Errorf("test failed: node not nil")
	}
}
func TestNodes3(t *testing.T) {
	ts := httptest.NewTLSServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.WriteHeader(http.StatusOK)
		b := []byte(`{
        {
            "uuid": "0505c414-78ab-47ad-90db-b4c1b5ea3c66",
            "name": "ddd",
            "parent_uuid": "beefbeef-beef-beef-beef-beefbeef0001",
            "parent_type": "global-system-config",
            "fq_name": [
                "default-global-system-config",
                "ddd"
            ],
            "display_name": "ddd",
            "href": "https://10.87.69.71:9091/node/0505c414-78ab-47ad-90db-b4c1b5ea3c66",
            "hostname": "ddd",
            "interface_name": "eth0",
            "ip_address": "1.1.1.1",
            "type": "private",
            "node_type": "private",
            "to": [
                "default-global-system-config",
                "ddd"
            ]
        }
    ]
}`)
		fmt.Fprintln(w, string(b))
	}))
	defer ts.Close()
	cmdURL := strings.TrimPrefix(ts.URL, "https://")

	ccClient := command.New(cmdURL,"user", "password", "cluster4", "IamAClusterToken")
	ccClient.AuthToken = "ThisIsValidToken"
	_, err := ccClient.Nodes()
	if err == nil {
		fmt.Println(err)
		t.Errorf("test failed: error not nil")
	}
}

func TestNodes4(t *testing.T) {
	ts := httptest.NewTLSServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.WriteHeader(http.StatusOK)
		b := []byte(`{
    "nodes": [
        {
            "node": {
                "uuid": "0505c414-78ab-47ad-90db-b4c1b5ea3c66",
                "name": "ddd",
                "parent_uuid": "beefbeef-beef-beef-beef-beefbeef0001",
                "parent_type": "global-system-config",
                "fq_name": [
                    "default-global-system-config",
                    "ddd"
                ],
                "display_name": "ddd",
                "type": "private",
                "node_type": "private",
                "ports": [
                    {
                        "uuid": "180cbbbb-7ca6-4e01-b984-58ce7e037d7d",
                        "name": "eth0",
                        "parent_uuid": "0505c414-78ab-47ad-90db-b4c1b5ea3c66",
                        "parent_type": "node",
                        "fq_name": [
                            "default-global-system-config",
                            "ddd",
                            "eth0"
                        ],
                        "display_name": "eth0",
                        "ip_address": "1.1.1.1",
                        "bms_port_info": {
                            "local_link_connection": {}
                        },
                        "esxi_port_info": {},
                        "to": [
                            "default-global-system-config",
                            "ddd",
                            "eth0"
                        ]
                    }
                ],
                "to": [
                    "default-global-system-config",
                    "ddd"
                ]
            }
        },
        {
            "node": {
                "uuid": "425074ec-e4a9-4513-8aa3-1bf65c06bb9b",
                "name": "5b11s1-node1-vm2",
                "parent_uuid": "beefbeef-beef-beef-beef-beefbeef0001",
                "parent_type": "global-system-config",
                "fq_name": [
                    "default-global-system-config",
                    "5b11s1-node1-vm2"
                ],
                "display_name": "5b11s1-node1-vm2",
                "hostname": "5b11s1-node1-vm2",
                "type": "private",
                "node_type": "private",
                "to": [
                    "default-global-system-config",
                    "5b11s1-node1-vm2"
                ]
            }
        }
    ]
}
`)
		fmt.Fprintln(w, string(b))
	}))
	defer ts.Close()
	cmdURL := strings.TrimPrefix(ts.URL, "https://")

	ccClient := command.New(cmdURL,"user", "password", "cluster4", "IamAClusterToken")
	ccClient.AuthToken = "ThisIsInValidToken"

	nodes, err := ccClient.Nodes()

	if err != nil {
		t.Errorf("test failed: error not nil")
		fmt.Println(err)
	}
	if len(nodes) != 2 {
		t.Errorf("test failed: node not nil")
	}
}
func TestNodes5(t *testing.T) {
	ccClient := command.New("2222.2.2.2.2:3332", "user", "password", "cluster4", "IamAClusterToken")
	ccClient.AuthToken = "ThisIsInValidToken"

	_, err := ccClient.Nodes()

	if err == nil {
		t.Errorf("test failed: error is nil")
	}
}
