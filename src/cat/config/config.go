// Package config provides primitives to manage basic contrail configuration
// objects.
package config

import (
	"bytes"
	"encoding/binary"
	"encoding/json"
	"fmt"
	"github.com/google/uuid"
	"io"
	"os"
	"strings"
	"time"

	"cat/types"
)

// FQNameTableType defines the map of type to uuid for contrail configuration.
type FQNameTableType map[string]map[string]string

// UUIDTableType defines the map of uuid to contents for contrail configuration.
type UUIDTableType map[string]map[string]string

// Ref represents a reference from one object to another.
type Ref struct {
	UUID string                 `json:"uuid"`
	Type string                 `json:"type"`
	Attr map[string]interface{} `json:"attr"`
}

// Child represents Parent:Child relation between two configuration objects.
type Child struct {
	UUID string `json:"uuid"`
	Type string `json:"type"`
}

// ContrailConfig represents a basic contrail configuration construct. Many
// configuration objects can be managed by just using this base construct.
type ContrailConfig struct {
	UUID       string
	Type       string   `json:"type"`
	ParentType string   `json:"parent_type"`
	FqName     []string `json:"fq_name"`

	Perms2      types.PermType2    `json:"prop:perms2"`
	IdPerms     types.IdPermsType `json:"prop:id_perms"`
	DisplayName string            `json:"prop:display_name"`
}

type ConfigMap map[string]*ContrailConfig

// Map creates a contrail configuration map.
func (c *ContrailConfig) Map(b []byte) (map[string]string, error) {
	v := map[string]interface{}{}
	conf := map[string]string{}

	d := json.NewDecoder(bytes.NewBuffer(b))
	d.UseNumber()
	if err := d.Decode(&v); err != nil {
		return nil, err
	}

	for key, _ := range v {
		switch {
		case strings.HasSuffix(key, "_refs"):
			if v[key] == nil {
				continue
			}
			refs, ok := v[key].([]interface{})
			if !ok {
				return nil, fmt.Errorf("%q invalid ref key %q", c.DisplayName, key)
			}
			for _, r := range refs {
				ref, ok := r.(map[string]interface{})
				if !ok {
					return nil, fmt.Errorf("%q invalid ref %q", c.DisplayName, ref)
				}
				k := fmt.Sprintf("ref:%v:%v", ref["type"], ref["uuid"])
				bytes, err := json.Marshal(ref["attr"])
				if err != nil {
					return nil, err
				}
				conf[k] = string(bytes)
			}

		case strings.HasSuffix(key, "_children"):
			if v[key] == nil {
				continue
			}
			children, ok := v[key].([]interface{})
			if !ok {
				return nil, fmt.Errorf("%q invalid children key %q", c.DisplayName, key)
			}
			for _, ch := range children {
				child, ok := ch.(map[string]interface{})
				if !ok {
					return nil, fmt.Errorf("%q invalid child %q", c.DisplayName, child)
				}
				k := fmt.Sprintf("children:%v:%v", child["type"], child["uuid"])
				conf[k] = "null"
			}

		default:
			bytes, err := json.Marshal(v[key])
			if err != nil {
				return nil, err
			}
			conf[key] = string(bytes)
		}
	}
	return conf, nil
}

// createContrailConfig creates a config object with type, fqname, timnestamp,
// owner information, etc.
func createContrailConfig(fqNameTable *FQNameTableType, tp, name, parentType string, fqName []string) (*ContrailConfig, error) {
	u, err := uuid.NewUUID()
	if err != nil {
		return nil, err
	}
	us := u.String()
	if (*fqNameTable)[tp] == nil {
		(*fqNameTable)[tp] = map[string]string{}
	}
	t := time.Now().String()
	ts := strings.ReplaceAll(t, " ", "T")
	c := ContrailConfig{
		UUID:        us,
		Type:        tp,
		ParentType:  parentType,
		DisplayName: name,
		Perms2: types.PermType2{
			Owner:        "cloud-admin",
			OwnerAccess:  7,
			GlobalAccess: 5,
		},
		IdPerms: types.IdPermsType{
			Enable: true,
			Uuid: &types.UuidType{
				UuidMslong: binary.BigEndian.Uint64(u[:8]),
				UuidLslong: binary.BigEndian.Uint64(u[8:]),
			},
			Created:      ts,
			LastModified: ts,
			UserVisible:  true,
			Permissions:  &types.PermType{
				Owner:       "cloud-admin",
				OwnerAccess: 7,
				OtherAccess: 7,
				Group:       "cloud-admin-group",
				GroupAccess: 7,
			},
			Description:  "",
			Creator:      "",
		},
		FqName: fqName,
	}
	(*fqNameTable)[tp][fmt.Sprintf("%s:%s", strings.Join(fqName, ":"), us)] = "null"
	return &c, nil
}

// GenerateDB dumps in memory configuration into file in json format.
func GenerateDB(fqNameTable *FQNameTableType, uuidTable *UUIDTableType, confFile string) error {
	file, err := os.Create(confFile)
	if err != nil {
		return err
	}
	defer file.Close()
	b1, err := json.Marshal(uuidTable)
	if err != nil {
		return nil
	}
	b2, _ := json.Marshal(fqNameTable)
	if err != nil {
		return nil
	}
	conf := fmt.Sprintf("{ \"cassandra\": { \"config_db_uuid\": { \"obj_uuid_table\": %s, \"obj_shared_table\": {}, \"obj_fq_name_table\": %s }, \"dm_keyspace\": { \"dm_pnf_resource_table\": {}, \"dm_pr_ae_id_table\": {}, \"dm_pr_vn_ip_table\": {} }, \"svc_monitor_keyspace\": { \"healthmonitor_table\": {}, \"loadbalancer_table\": {}, \"pool_table\": {}, \"service_instance_table\": {}}}, \"zookeeper\": \"[]\"}\n", string(b1), string(b2))
	_, err = io.WriteString(file, conf)
	return file.Sync()
}

// NewConfigObject creates a new configuration object and updates the map.
func NewConfigObject(fqNameTable *FQNameTableType, uuidTable *UUIDTableType, tp, name, parent string, fqname []string) (*ContrailConfig, error) {
	obj, err := createContrailConfig(fqNameTable, tp, name, parent, fqname)
	if err != nil {
		return nil, err
	}
	obj.UpdateDB(uuidTable)
	return obj, nil
}

// UpdateDB updates uuid map with the contents as a json string.
func (c *ContrailConfig) UpdateDB(uuidTable *UUIDTableType) error {
	b, err := json.Marshal(c)
	if err != nil {
		return nil
	}

	j, err := c.Map(b)
	if err != nil {
		return err
	}
	(*uuidTable)[c.UUID] = j
	return nil
}

// Delete deletes a uuid key (row) from the uuid based config map. fqname table
// is also updated.
func (c *ContrailConfig) Delete(fqNameTable *FQNameTableType, uuidTable *UUIDTableType) error {
	delete(*uuidTable, c.UUID)
	delete((*fqNameTable)[c.Type], fmt.Sprintf("%s:%s", strings.Join(c.FqName, ":"), c.UUID))
	return nil
}
