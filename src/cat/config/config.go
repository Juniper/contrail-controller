/*
 * copyright (c) 2019 juniper networks, inc. all rights reserved.
 */

package config

import (
    "cat/types"
    "encoding/binary"
    "encoding/json"
    "fmt"
    "io"
    "os"
    "github.com/google/uuid"
    "strings"
    "time"
)

type FQNameTableType map[string]map[string]string
type UUIDTableType map[string]map[string]string

// Ref represents a reference from one object to another.
type Ref struct {
    UUID string `json:"uuid"`
    Type string `json:"type"`
    Attr map[string]interface{} `json:"attr"`
}

// Child represents Parent:Child relation between two configuration objects.
type Child struct {
    UUID string `json:"uuid"`
    Type string `json:"type"`
}

// ContrailConfig represents a basic contrail configuration construct. Many configuration objects can be managed by just using this base construct.
type ContrailConfig struct {
    UUID string
    Type string `json:"type"`
    ParentType string `json:"parent_type"`
    FqName []string `json:"fq_name"`

    Perms2 types.PermType `json:"prop:perms2"`
    IdPerms types.IdPermsType `json:"prop:id_perms"`
    DisplayName string `json:"prop:display_name"`
}

func (c *ContrailConfig) Map (b []byte) (map[string]string, error) {
    v := map[string]interface{}{}
    json.Unmarshal(b, &v)
    conf := map[string]string{}
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

func createContrailConfig (fqNameTable *FQNameTableType, tp, name, parent_type string, fq_name []string) (*ContrailConfig, error) {
    u, err := uuid.NewUUID()
    if err != nil {
        return nil, err
    }
    us := u.String()
    if (*fqNameTable)[tp] == nil {
        (*fqNameTable)[tp] = map[string]string{}
    }
    ts := time.Now().String()
    c := ContrailConfig{
        UUID: us,
        Type: tp,
        ParentType: parent_type,
        DisplayName: name,
        Perms2: types.PermType {
            Owner: "cloud-admin",
            OwnerAccess: 0,
        },
        IdPerms: types.IdPermsType {
            Enable: true,
            Uuid: &types.UuidType {
                UuidMslong: binary.BigEndian.Uint64(u[:8]),
                UuidLslong: binary.BigEndian.Uint64(u[8:]),
            },
            Created: ts,
            LastModified: ts,
            UserVisible: true,
        },
        FqName: fq_name,
    }
    (*fqNameTable)[tp][fmt.Sprintf("%s:%s", strings.Join(fq_name, ":"), us)] = "null"
    return &c, nil
}

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
    conf := fmt.Sprintf("[{ \"operation\": \"db_sync\", \"db\": %s, \"OBJ_FQ_NAME_TABLE\": %s}]\n", string(b1), string(b2))
    _, err = io.WriteString(file, conf)
    return file.Sync()
}

func NewConfigObject (fqNameTable *FQNameTableType, uuidTable *UUIDTableType, tp, name, parent string, fqname []string) (*ContrailConfig, error) {
    obj, err := createContrailConfig(fqNameTable, tp, name, parent, fqname)
    if err != nil {
        return nil, err
    }
    obj.UpdateDB(uuidTable)
    return obj, nil
}

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
