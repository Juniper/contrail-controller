// Package cat provides integration testing framework for control-nodes, agents
// and CRPDs. Each control-node and agent runs as a separate unix process.
// Each CRPD runs inside a docker container. Framework hangles the confguration
// and life cycle management of all these objects.
package cat

import (
	"archive/tar"
	"bufio"
	"compress/gzip"
	"container/list"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"io/ioutil"
	"net"
	"net/http"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"strings"
	"time"

	"cat/agent"
	"cat/config"
	"cat/controlnode"
	"cat/crpd"
	"cat/sut"

	log "github.com/sirupsen/logrus"
)

// CAT is the main place-holder object of all other objects managed.
type CAT struct {
	SUT          sut.Component
	ControlNodes []*controlnode.ControlNode
	Agents       []*agent.Agent
	CRPDs        []*crpd.CRPD
	FqNameTable  config.FQNameTableType
	UuidTable    config.UUIDTableType
	ConfigMap    config.ConfigMap
}

// Timestamp format for logfiles.
const timestamp = "20060102_150405"

// Utiulity command to load CRPD docker image dynamically.
const crpdImageGetCommand = "sshpass -p c0ntrail123 ssh 10.84.5.39 cat /cs-shared/crpd/crpd.tgz | sudo --non-interactive docker load"

// File where release numbers of previous releases are stored
const previousReleases  = "../../../../controller/src/cat/release_list"

// Link to download binaries of previous releases
const binaryLink  = "http://svl-artifactory.juniper.net/artifactory/contrail-static-prod"

// New creates and initializes a CAT instance.
func New() (*CAT, error) {
	c := &CAT{
		ControlNodes: []*controlnode.ControlNode{},
		Agents:       []*agent.Agent{},
		CRPDs:        []*crpd.CRPD{},
		FqNameTable:  config.FQNameTableType{},
		UuidTable:    config.UUIDTableType{},
		ConfigMap:    config.ConfigMap{},
	}
	now := time.Now()

	cwd, err := os.Getwd()
	if err != nil {
		return nil, fmt.Errorf("Cannot find present working directory: %v", err)
	}
	c.SUT.Manager.RootDir = filepath.Join(cwd+"../../../../build/debug/cat", now.Format(timestamp))
	if err := os.MkdirAll(c.SUT.Manager.RootDir, 0700); err != nil {
		return nil, fmt.Errorf("failed to create rootdir %q :%v", c.SUT.Manager.RootDir, err)
	}
	c.SUT.Manager.ReportDir = filepath.Join(c.SUT.Manager.RootDir, "reports")
	err = os.MkdirAll(c.SUT.Manager.ReportDir, 0700)
	if err != nil {
		return nil, fmt.Errorf("failed to make report directory: %v", err)
	}
	c.setHostIP()

	// Check whether CRPD can be used in this test environment.
	if crpd.CanUseCRPD() {
		cmd := exec.Command("sudo", "--non-interactive", "/usr/bin/docker", "image", "inspect", "crpd")
		if _, err := cmd.Output(); err != nil {
			// Setup crpd docker image.
			cmd := exec.Command("/bin/bash", "-c", crpdImageGetCommand)
			if _, err := cmd.Output(); err != nil {
				return nil, fmt.Errorf("Cannot load crpd docker image")
			}
		}
	}
	log.Infof("Test data in %q", c.SUT.Manager.RootDir)
	return c, err
}

// Teardown stops all components and closes down the CAT instance.
func (c *CAT) Teardown() error {
	for _, cn := range c.ControlNodes {
		if err := cn.Teardown(); err != nil {
			return err
		}
	}
	for _, a := range c.Agents {
		if err := a.Teardown(); err != nil {
			return err
		}
	}

	for _, cr := range c.CRPDs {
		if err := cr.Teardown(); err != nil {
			return err
		}
	}
	return nil
}

// setHostIP finds self host ip address.
func (c *CAT) setHostIP() error {
	cmd := exec.Command("hostname", "-i")
	out, err := cmd.CombinedOutput()
	if err != nil {
		log.Fatal("Cannot find host ip address")
		return err
	}

	ips := strings.Split(string(out), " ")
	for _, ip := range ips {
		if !strings.HasPrefix(ip, "127.") {
			c.SUT.Manager.IP = strings.Trim(ip, "\n")
			return nil
		}
	}
	return errors.New("Cannot retrieve host ip address")
}

// AddAgent creates a contrail-vrouter-agent object and starts the mock agent
// process in background.
func (c *CAT) AddAgent(test string, name, binary string, control_nodes []*controlnode.ControlNode) (*agent.Agent, error) {
	endpoints := []sut.Endpoint{}
	for _, control_node := range control_nodes {
		endpoints = append(endpoints, sut.Endpoint{
			IP:   control_node.IPAddress,
			Port: control_node.Config.XMPPPort,
		})
	}
	agent, err := agent.New(c.SUT.Manager, name, binary, test, endpoints)
	if err != nil {
		return nil, fmt.Errorf("failed create agent: %v", err)
	}
	c.Agents = append(c.Agents, agent)
	return agent, nil
}

// AddControlNode creates a contrail-control object and starts the mock
// control-node process in the background.
func (c *CAT) AddControlNode(test, name, ip_address, conf_file string, bgp_port int) (*controlnode.ControlNode, error) {
	cn, err := controlnode.New(c.SUT.Manager, name, ip_address, conf_file, test, bgp_port)
	if err != nil {
		return nil, fmt.Errorf("failed to create control-node: %v", err)
	}
	cn.Verbose = c.SUT.Manager.Verbose
	c.ControlNodes = append(c.ControlNodes, cn)
	log.Infof("Started %s at http://%s:%d\n", cn.Name, c.SUT.Manager.IP, cn.Config.HTTPPort)
	return cn, nil
}

// AddCRPD creates a CRPD object and starts the CRPD docker container process
// in the background.
func (c *CAT) AddCRPD(test, name string) (*crpd.CRPD, error) {
	cr, err := crpd.New(c.SUT.Manager, name, test)
	if err != nil {
		return nil, fmt.Errorf("failed to create crpd: %v", err)
	}
	c.CRPDs = append(c.CRPDs, cr)
	return cr, nil
}

func GetFreePort() (int, error) {
	addr, err := net.ResolveTCPAddr("tcp", "localhost:0")
	if err != nil {
		return 0, err
	}

	l, err := net.ListenTCP("tcp", addr)
	if err != nil {
		return 0, err
	}
	defer l.Close()
	return l.Addr().(*net.TCPAddr).Port, nil
}

func ReplacePortNumbers(input string, port int) (out string) {
	p := fmt.Sprintf("\"port\":%d", port)

	re1 := regexp.MustCompile("\"port\":null")
	val1 := re1.ReplaceAllString(input, p)
	re2 := regexp.MustCompile("\"port\":[[:digit:]]{3,5}")
	val2 := re2.ReplaceAllString(val1, p)

	re3 := regexp.MustCompile("\"port\": null")
	val3 := re3.ReplaceAllString(val2, p)
	re4 := regexp.MustCompile("\"port\": [[:digit:]]{3,5}")
	val4 := re4.ReplaceAllString(val3, p)

	return val4
}

func ReplaceAddress(input string, address string) (out string) {

	a := fmt.Sprintf("\"address\":\"%s\"", address)
	id := fmt.Sprintf("\"identifier\":\"%s\"", address)

	re3 := regexp.MustCompile(`(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)(\.(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)){3}`)
	val3 := re3.ReplaceAllString(input, address)

	re1 := regexp.MustCompile("\"address\":null")
	val1 := re1.ReplaceAllString(val3, a)
	re2 := regexp.MustCompile("\"address\": null")
	val2 := re2.ReplaceAllString(val1, a)

	re4 := regexp.MustCompile("\"identifier\":null")
	val4 := re4.ReplaceAllString(val2, id)
	re5 := regexp.MustCompile("\"identifier\": null")
	val5 := re5.ReplaceAllString(val4, id)

	return val5
}

// Parses the config file and changes the BGP port number and ip-address 
// of the control-nodes
// Returns a list of control-nodes with new BGP port-number and ip-address
func GetNumOfControlNodes() (ConNodesDS map[string]interface{}, err error) {
	var confile string
	confile = controlnode.GetConfFile()
	jsonFile, err := os.Open(confile)
	if err != nil {
		return nil, fmt.Errorf("failed to open ConfFile: %v", err)
	}

	defer jsonFile.Close()
	byteValue, _ := ioutil.ReadAll(jsonFile)
	var result map[string]interface{}
	json.Unmarshal([]byte(byteValue), &result)

	ConNodesDS = make(map[string]interface{})

	cassandra := result["cassandra"].(map[string]interface{})
	config_db_uuid := cassandra["config_db_uuid"].(map[string]interface{})
	fq_name_table := config_db_uuid["obj_fq_name_table"].(map[string]interface{})
	bgp_routers := fq_name_table["bgp_router"].(map[string]interface{})
	uuid_table := config_db_uuid["obj_uuid_table"].(map[string]interface{})
	i := 1
	ipoctet := 127
	for key := range bgp_routers {
		re := regexp.MustCompile(":")
		val := re.Split(key, -1)
		control_node := uuid_table[val[5]].(map[string]interface{})
		router_params := control_node["prop:bgp_router_parameters"].([]interface{})
		params := fmt.Sprintf("%v", router_params[0])
		port, _ := GetFreePort()
		address := fmt.Sprintf("%d.0.0.%d", ipoctet, i)

		//Replace Port Numbers
		val2 := ReplacePortNumbers(params, port)

		//Replace address and identifier
		val3 := ReplaceAddress(val2, address)

		router_params[0] = val3
		control_node["prop:bgp_router_parameters"] = router_params
		uuid_table[val[5]] = control_node
		m := make(map[string]interface{})
		m["port"] = port
		m["address"] = address
		ConNodesDS[val[4]] = m
		i++
	}
	config_db_uuid["obj_uuid_table"] = uuid_table
	cassandra["config_db_uuid"] = config_db_uuid
	result["cassandra"] = cassandra

	write, _ := json.Marshal(result)
	err = ioutil.WriteFile(confile, write, os.ModePerm)
	jsonFile.Sync()
	return ConNodesDS, nil
}

func GetPreviousReleases() (Releases list.List, err error) {
    file, err := os.Open(previousReleases)
    if err != nil {
        log.Fatal(err)
    }
    defer file.Close()

    scanner := bufio.NewScanner(file)
    for scanner.Scan() {
        rel := scanner.Text()
        Releases.PushBack(rel)
    }

    if err := scanner.Err(); err != nil {
        log.Fatal(err)
    }
    return Releases, nil
}

func GetAgentBinary(c *CAT, release string) (binary_path string) {
    path := fmt.Sprintf("%s/%s/contrail-vrouter-agent.tgz", binaryLink, release)
    out := fmt.Sprintf("%s/contrail-vrouter-agent.tgz", c.SUT.Manager.RootDir)

    err := DownloadFile(path, out)
    if err != nil {
        log.Fatal(err)
    }

    err = os.Chmod(out, 0777)
    r, err := os.Open(out)
    if err != nil {
        fmt.Println("error")
    }
    ExtractTarGz(c.SUT.Manager.RootDir, r)
    ext := strings.ReplaceAll(release, "/", ".")
    ext = strings.ReplaceAll(ext, "R", "")
    name := strings.ReplaceAll("contrail-vrouter-agent", "-", ".")
    binary_path = fmt.Sprintf("%s/%s.%s", c.SUT.Manager.RootDir, name, ext)
    return
}

func ExtractTarGz(path string, gzipStream io.Reader) {
    uncompressedStream, err := gzip.NewReader(gzipStream)
    if err != nil {
        log.Fatal("ExtractTarGz: NewReader failed")
    }

    tarReader := tar.NewReader(uncompressedStream)

    for true {
        header, err := tarReader.Next()

        if err == io.EOF {
            break
        }

        if err != nil {
            log.Fatalf("ExtractTarGz: Next() failed: %s", err.Error())
        }

        switch header.Typeflag {
        case tar.TypeDir:
            if err := os.Mkdir(fmt.Sprintf("%s/%s", path, strings.ReplaceAll(header.Name, "-", ".")), 0755); err != nil {
                log.Fatalf("ExtractTarGz: Mkdir() failed: %s", err.Error())
            }
        case tar.TypeReg:
            name := fmt.Sprintf("%s/%s", path, strings.ReplaceAll(header.Name, "-", "."))
            outFile, err := os.Create(name)
            if err != nil {
                log.Fatalf("ExtractTarGz: Create() failed: %s", err.Error())
            }
            if _, err := io.Copy(outFile, tarReader); err != nil {
                log.Fatalf("ExtractTarGz: Copy() failed: %s", err.Error())
            }
            outFile.Close()

        default:
            log.Fatalf(
                "ExtractTarGz: unknown type: %s in %s",
                header.Typeflag,
                header.Name)
        }

    }
}

func DownloadFile(url string, filepath string) error {
    // Create the file
    out, err := os.Create(filepath)
    if err != nil {
        return err
    }
    defer out.Close()

    // Get the data
    resp, err := http.Get(url)
    if err != nil {
        return err
    }
    defer resp.Body.Close()

    // Write the body to file
    _, err = io.Copy(out, resp.Body)
    if err != nil {
        return err
    }

    return nil
}

func CheckIfApplicable() (err error) {
    path := binaryLink
    _, err = http.Get(path)
    return err
}

func GetParamsAddPort(vmi_id string) (vm_id, vn_id, proj_id, vm_name string, err error) {
        confile := controlnode.GetConfFile()
        jsonFile, err := os.Open(confile)
        if err != nil {
                return "", "", "", "", fmt.Errorf("failed to open ConfFile: %v", err)
        }

        byteValue, _ := ioutil.ReadAll(jsonFile)
        jsonFile.Close()
        var result map[string]interface{}

	json.Unmarshal([]byte(byteValue), &result)
        cassandra := result["cassandra"].(map[string]interface{})
        config_db_uuid := cassandra["config_db_uuid"].(map[string]interface{})
        re := regexp.MustCompile(":")
        uuid_table := config_db_uuid["obj_uuid_table"].(map[string]interface{})
        vmis := uuid_table[vmi_id].(map[string]interface{})
        for key := range vmis {
                if strings.Contains(key, "ref:virtual_machine") {
                        val1 := re.Split(key, -1)
                        vm_id = val1[2]
                        continue
                }
                if strings.Contains(key, "ref:virtual_network") {
                        val1 := re.Split(key, -1)
                        vn_id = val1[2]
                        continue
                }
                if strings.Contains(key, "parent:project") {
                        val1 := re.Split(key, -1)
                        proj_id = val1[2]
                        continue
                }
        }

        fq_name_table := config_db_uuid["obj_fq_name_table"].(map[string]interface{})
        vms := fq_name_table["virtual_machine"].(map[string]interface{})
        for key := range vms {
                val := re.Split(key, -1)
                if val[1] == vm_id {
                        vm_name = val[0]
                        break
                }
        }

        return vm_id, vn_id, proj_id, vm_name, nil
}

