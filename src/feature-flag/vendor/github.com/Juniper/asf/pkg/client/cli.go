package client

import (
	"context"
	"net/http"
	"net/url"
	"path/filepath"
	"strconv"
	"time"

	"github.com/Juniper/asf/pkg/fileutil"
	"github.com/Juniper/asf/pkg/keystone"
	"github.com/Juniper/asf/pkg/logutil"
	"github.com/Juniper/asf/pkg/models/basemodels"
	"github.com/Juniper/asf/pkg/schema"
	"github.com/Juniper/asf/pkg/services/baseservices"
	"github.com/flosch/pongo2"
	"github.com/pkg/errors"
	"github.com/sirupsen/logrus"
	"github.com/spf13/viper"

	yaml "gopkg.in/yaml.v2"
)

// YAML key names
const (
	DataKey      = "data"
	KindKey      = "kind"
	ResourcesKey = "resources"
)

const (
	retryMax         = 5
	serverSchemaFile = "schema.json"
)

// CLI represents API Server's command line interface.
type CLI struct {
	HTTP

	schemaRoot string
	log        *logrus.Entry
}

// NewCLIByViper returns new logged in CLI client using Viper configuration.
func NewCLIByViper() (*CLI, error) {
	return NewCLI(
		&HTTPConfig{
			ID:       viper.GetString("client.id"),
			Password: viper.GetString("client.password"),
			Endpoint: viper.GetString("client.endpoint"),
			AuthURL:  viper.GetString("keystone.authurl"),
			Scope: keystone.NewScope(
				viper.GetString("client.domain_id"),
				viper.GetString("client.domain_name"),
				viper.GetString("client.project_id"),
				viper.GetString("client.project_name"),
			),
			Insecure: viper.GetBool("insecure"),
		},
		viper.GetString("client.schema_root"),
	)
}

// NewCLI returns new logged in CLI Client.
func NewCLI(c *HTTPConfig, schemaRoot string) (*CLI, error) {
	client := NewHTTP(c)

	if err := client.Login(context.Background()); err != nil {
		return nil, err
	}

	return &CLI{
		HTTP:       *client,
		schemaRoot: schemaRoot,
		log:        logutil.NewLogger("cli"),
	}, nil
}

// ShowResource shows resource with given schemaID and UUID.
func (c *CLI) ShowResource(schemaID, uuid string) (string, error) {
	if schemaID == "" || uuid == "" {
		return c.showHelp(schemaID, showHelpTemplate)
	}

	var response map[string]interface{}
	_, err := c.Read(context.Background(), urlPath(schemaID, uuid), &response)
	if err != nil {
		return "", err
	}

	data, ok := response[basemodels.SchemaIDToKind(schemaID)].(map[string]interface{})
	if !ok {
		return "", errors.Errorf(
			"resource in response is not a JSON object: %v",
			response[basemodels.SchemaIDToKind(schemaID)],
		)
	}

	return encodeToYAML(syncListResponse{Resources: []syncResponse{{Data: data, Kind: schemaID}}})
}

type syncData struct {
	Operation string      `json:"operation" yaml:"operation"`
	Kind      string      `json:"kind" yaml:"kind"`
	Data      interface{} `json:"data" yaml:"data"`
}

type syncListData struct {
	Resources []syncData `json:"resources" yaml:"resources"`
}

type syncResponse = syncData
type syncListResponse = syncListData

type syncListRequest = syncListData

const showHelpTemplate = `Show command possible usages:
{% for schema in schemas %}contrail show {{ schema.ID }} $UUID
{% endfor %}`

// ListParameters contains parameters for list command.
type ListParameters struct {
	Filters      string
	PageLimit    int64
	PageMarker   string
	Detail       bool
	Count        bool
	Shared       bool
	ExcludeHRefs bool
	ParentFQName string
	ParentType   string
	ParentUUIDs  string
	BackrefUUIDs string
	// TODO(Daniel): handle RefUUIDs
	ObjectUUIDs string
	Fields      string
}

// Resources define output format of list command.
type Resources = map[string][]map[string]interface{}

// ListResources lists resources with given schemaID using filters.
func (c *CLI) ListResources(schemaID string, lp *ListParameters) (string, error) {
	if schemaID == "" {
		return c.showHelp("", listHelpTemplate)
	}

	var response map[string]interface{}
	if _, err := c.ReadWithQuery(
		context.Background(),
		pluralPath(schemaID),
		queryParameters(lp),
		&response,
	); err != nil {
		return "", err
	}

	var r Resources
	var err error
	switch {
	case lp.Count:
		return encodeToYAML(response)
	case lp.Detail:
		r, err = makeOutputResourcesFromDetailedResponse(schemaID, response)
	default:
		r, err = makeOutputResources(schemaID, response)
	}
	if err != nil {
		return "", err
	}

	return encodeToYAML(r)
}

const listHelpTemplate = `List command possible usages:
{% for schema in schemas %}contrail list {{ schema.ID }}
{% endfor %}`

func pluralPath(schemaID string) string {
	return "/" + basemodels.SchemaIDToKind(schemaID) + "s"
}

func queryParameters(lp *ListParameters) url.Values {
	values := url.Values{}
	for k, v := range map[string]string{
		baseservices.FiltersKey:      lp.Filters,
		baseservices.PageLimitKey:    strconv.FormatInt(lp.PageLimit, 10),
		baseservices.PageMarkerKey:   lp.PageMarker,
		baseservices.DetailKey:       strconv.FormatBool(lp.Detail),
		baseservices.CountKey:        strconv.FormatBool(lp.Count),
		baseservices.SharedKey:       strconv.FormatBool(lp.Shared),
		baseservices.ExcludeHRefsKey: strconv.FormatBool(lp.ExcludeHRefs),
		baseservices.ParentFQNameKey: lp.ParentFQName,
		baseservices.ParentTypeKey:   lp.ParentType,
		baseservices.ParentUUIDsKey:  lp.ParentUUIDs,
		baseservices.BackrefUUIDsKey: lp.BackrefUUIDs,
		// TODO(Daniel): handle RefUUIDs
		baseservices.ObjectUUIDsKey: lp.ObjectUUIDs,
		baseservices.FieldsKey:      lp.Fields,
	} {
		if !isZeroValue(v) {
			values.Set(k, v)
		}
	}
	return values
}

func isZeroValue(value interface{}) bool {
	return value == "" || value == 0 || value == false
}

// makeOutputResourcesFromDetailedResponse creates list command output in format compatible with Sync command input
// based on API Server detailed response.
func makeOutputResourcesFromDetailedResponse(schemaID string, response map[string]interface{}) (Resources, error) {
	r := Resources{}
	for _, rawList := range response {
		list, ok := rawList.([]interface{})
		if !ok {
			return nil, errors.Errorf("detailed response should contain list of resources: %v", rawList)
		}

		for _, rawWrappedObject := range list {
			wrappedObject, ok := rawWrappedObject.(map[string]interface{})
			if !ok {
				return nil, errors.Errorf("detailed response contains invalid data: %v", rawWrappedObject)
			}

			for _, object := range wrappedObject {
				r[ResourcesKey] = append(r[ResourcesKey], map[string]interface{}{
					KindKey: schemaID,
					DataKey: object,
				})
			}
		}
	}
	return r, nil
}

// makeOutputResources creates list command output in format compatible with Sync command input
// based on API Server standard response.
func makeOutputResources(schemaID string, response map[string]interface{}) (Resources, error) {
	r := Resources{}
	for _, rawList := range response {
		list, ok := rawList.([]interface{})
		if !ok {
			return nil, errors.Errorf("response should contain list of resources: %v", rawList)
		}

		for _, object := range list {
			r[ResourcesKey] = append(r[ResourcesKey], map[string]interface{}{
				KindKey: schemaID,
				DataKey: object,
			})
		}
	}
	return r, nil
}

// SyncResources synchronizes state of resources specified in given file.
func (c *CLI) SyncResources(filePath string) (string, error) {
	var req syncListRequest
	if err := fileutil.LoadFile(filePath, &req); err != nil {
		return "", err
	}
	for i := range req.Resources {
		req.Resources[i].Data = fileutil.YAMLtoJSONCompat(req.Resources[i].Data)
	}

	var response []syncResponse
	if _, err := c.Create(context.Background(), "/sync", req, &response); err != nil {
		return "", err
	}

	return encodeToYAML(syncListResponse{Resources: response})
}

// SetResourceParameter sets parameter value of resource with given schemaID na UUID.
func (c *CLI) SetResourceParameter(schemaID, uuid, yamlString string) (string, error) {
	if schemaID == "" || uuid == "" {
		return c.showHelp(schemaID, setHelpTemplate)
	}

	var data map[string]interface{}
	if err := yaml.Unmarshal([]byte(yamlString), &data); err != nil {
		return "", err
	}
	data["uuid"] = uuid

	_, err := c.Update(
		context.Background(),
		urlPath(schemaID, uuid),
		map[string]interface{}{
			basemodels.SchemaIDToKind(schemaID): fileutil.YAMLtoJSONCompat(data),
		},
		nil,
	)
	if err != nil {
		return "", err
	}

	return c.ShowResource(schemaID, uuid)
}

const setHelpTemplate = `Set command possible usages:
{% for schema in schemas %}contrail set {{ schema.ID }} $UUID $YAML
{% endfor %}`

// DeleteResource deletes resource with given schemaID and UUID.
func (c *CLI) DeleteResource(schemaID, uuid string) (string, error) {
	if schemaID == "" || uuid == "" {
		return c.showHelp(schemaID, removeHelpTemplate)
	}

	response, err := c.EnsureDeleted(context.Background(), urlPath(schemaID, uuid), nil)
	if err != nil {
		return "", err
	}

	if response.StatusCode == http.StatusNotFound {
		c.log.WithField("path", urlPath(schemaID, uuid)).Debug("Not found")
	}

	return "", nil
}

const removeHelpTemplate = `Remove command possible usages:
{% for schema in schemas %}contrail rm {{ schema.ID }} $UUID
{% endfor %}`

type deleteRequest struct {
	Kind string `json:"kind" yaml:"kind"`
	Data struct {
		UUID string `json:"uuid" yaml:"uuid"`
	} `json:"data" yaml:"data"`
}

type deleteListRequest struct {
	List []deleteRequest `json:"resources" yaml:"resources"`
}

// DeleteResources deletes multiple resources specified in given file.
func (c *CLI) DeleteResources(filePath string) (string, error) {
	var request deleteListRequest
	if err := fileutil.LoadFile(filePath, &request); err != nil {
		return "", nil
	}

	for _, r := range request.List {
		if _, err := c.DeleteResource(r.Kind, r.Data.UUID); err != nil {
			return "", err
		}
	}

	return "", nil
}

func urlPath(schemaID, uuid string) string {
	return "/" + basemodels.SchemaIDToKind(schemaID) + "/" + uuid
}

// ShowSchema returns schema with with given schemaID.
func (c *CLI) ShowSchema(schemaID string) (string, error) {
	return c.showHelp(schemaID, schemaTemplate)
}

const schemaTemplate = `
{% for schema in schemas %}
# {{ schema.Title }} {{ schema.Description }}
- kind: {{ schema.ID }}
  data: {% for key, value in schema.JSONSchema.Properties %}
    {{ key }}: {{ value.Default }} # {{ value.Title }} ({{ value.Type }}) {% endfor %}
{% endfor %}`

func (c *CLI) showHelp(schemaID string, template string) (string, error) {
	api, err := c.fetchServerAPI(filepath.Join(c.schemaRoot, serverSchemaFile))
	if err != nil {
		return "", err
	}

	if schemaID != "" {
		s := api.SchemaByID(schemaID)
		if s == nil {
			return "", errors.Errorf("schema %s not found", schemaID)
		}
		api.Schemas = []*schema.Schema{s}
	}

	tpl, err := pongo2.FromString(template)
	if err != nil {
		return "", err
	}

	o, err := tpl.Execute(pongo2.Context{"schemas": api.Schemas})
	if err != nil {
		return "", err
	}

	return o, nil
}

func (c *CLI) fetchServerAPI(serverSchema string) (*schema.API, error) {
	var api schema.API
	for i := 0; i < retryMax; i++ {
		_, err := c.Read(context.Background(), serverSchema, &api)
		if err == nil {
			break
		}

		logrus.WithError(err).Warn("Failed to connect API Server - reconnecting")
		time.Sleep(time.Second)
	}

	return &api, nil
}

func encodeToYAML(data interface{}) (string, error) {
	o, err := yaml.Marshal(data)
	if err != nil {
		return "", err
	}

	return string(o), nil
}
