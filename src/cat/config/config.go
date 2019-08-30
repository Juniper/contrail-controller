/*
 * copyright (c) 2019 juniper networks, inc. all rights reserved.
 */

package config

import (
    "cat/types"
    "encoding/json"
    "fmt"
    "io"
    "os"
    "github.com/google/uuid"
    "strings"
)

// ./src/contrail-api-client/generateds/generateDS.py -f -o ~/go/src/github.com/Juniper/contrail-go-api/types -g golang-api src/contrail-api-client/schema/vnc_cfg.xsd

var FQNameTable map[string]map[string]string
var UUIDTable map[string]map[string]string

type Ref struct {
    Uuid string `json:"uuid"`
    Type string `json:"type"`
    Attr map[string]interface{} `json:"attr"`
}

type Child struct {
    Uuid string `json:"uuid"`
    Type string `json:"type"`
}

type ContrailConfigObject struct {
    Uuid string
    Type string `json:"type"`
    ParentType string `json:"parent_type"`
    FqName []string `json:"fq_name"`

    Perms2 types.PermType `json:"prop:perms2"`
    IdPerms types.IdPermsType `json:"prop:id_perms"`
    DisplayName string `json:"prop:display_name"`
}

func Initialize() {
    FQNameTable = make(map[string]map[string]string)
    UUIDTable = make(map[string]map[string]string)
}

func (self *ContrailConfigObject) ToJson (b []byte) (map[string]string, error) {
    var v map[string]interface{}
    json.Unmarshal(b, &v)
    json_strings := make(map[string]string)
    for key, _ := range v {
        if strings.HasSuffix(key, "_refs") {
            if v[key] == nil {
                continue
            }
            refs := v[key].([]interface{})
            for i := range refs {
                ref := refs[i].(map[string]interface{})
                k := fmt.Sprintf("ref:%s:%s", ref["type"].(string), ref["uuid"].(string))
                c, err := json.Marshal(ref["attr"])
                if err != nil {
                    return nil, err
                }
                json_strings[k] = string(c)
            }
        } else if strings.HasSuffix(key, "_children") {
            if v[key] == nil {
                continue
            }
            children := v[key].([]interface{})
            for i := range children {
                child := children[i].(map[string]interface{})
                k := fmt.Sprintf("children:%s:%s", child["type"].(string), child["uuid"].(string))
                json_strings[k] = "null"
            }
        } else {
            b, err := json.Marshal(v[key])
            if err != nil {
                return nil, err
            }
            json_strings[key] = string(b)
        }
    }
    return json_strings, nil
}

func createContrailConfigObject (tp, name, parent_type string, fq_name []string) (*ContrailConfigObject, error) {
    u, err := uuid.NewUUID()
    if err != nil {
        return nil, err
    }
    us := u.String()
    if FQNameTable[tp] == nil {
        FQNameTable[tp] = make(map[string]string)
    }
    c := ContrailConfigObject{
        Uuid: us,
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
                UuidMslong: 7845966321683075561,
                UuidLslong: 11696129868600660048,
            },
            Created: "2019-08-20T15:25:33.570592",
            LastModified: "2019-08-20T15:25:33.570617",
            UserVisible: true,
        },
        FqName: fq_name,
    }
    FQNameTable[tp][fmt.Sprintf("%s:%s", strings.Join(fq_name, ":"), us)] = "null"
    return &c, nil
}

func GenerateDB(confFile string) error {
    file, err := os.Create(confFile)
    if err != nil {
        return err
    }
    defer file.Close()
    b1, err := json.Marshal(UUIDTable)
    if err != nil {
        return nil
    }
    b2, _ := json.Marshal(FQNameTable)
    if err != nil {
        return nil
    }
    conf := fmt.Sprintf("[{ \"operation\": \"db_sync\", \"db\": %s, \"OBJ_FQ_NAME_TABLE\": %s}]\n", string(b1), string(b2))
    _, err = io.WriteString(file, conf)
    return file.Sync()
}

func NewConfigObject (tp, name, parent string, fqname []string) (*ContrailConfigObject, error) {
    obj, err := createContrailConfigObject(tp, name, parent, fqname)
    if err != nil {
        return nil, err
    }
    obj.UpdateDB()
    return obj, nil
}

func (c *ContrailConfigObject) UpdateDB() error {
    b, err := json.Marshal(c)
    if err != nil {
        return nil
    }

    j, err := c.ToJson(b)
    if err != nil {
        return err
    }
    UUIDTable[c.Uuid] = j
    return nil
}
