package schema

import (
	"encoding/json"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"syscall"
	"time"

	"github.com/Juniper/asf/pkg/fileutil"
	"github.com/Juniper/asf/pkg/format"
	"github.com/pkg/errors"
	"github.com/sirupsen/logrus"

	yaml "gopkg.in/yaml.v2"
)

//Version is version for schema format.
var Version = "1.0"

const (
	schemaStartIndex    = 10
	schemaIndexOffset   = 3
	propertyIndexOffset = 1000
	stringType          = "string"
)

// Available type values.
const (
	AbstractType = "abstract"
	ObjectType   = "object"
	IntegerType  = "integer"
	UintType     = "uint64"
	ArrayType    = "array"
	BooleanType  = "boolean"
	NumberType   = "number"
	StringType   = stringType
	Base64Type   = "base64"
)

// Available Go type values.
const (
	IntGoType   = "int64"
	UintGoType  = "uint64"
	FloatGoType = "float64"
)

// Available Proto type values
const (
	IntProtoType   = IntGoType
	FloatProtoType = "float"
)

const (
	maxColumnLen = 55
	//RefPrefix is table column name prefix for reference
	RefPrefix = "ref"
	//ParentPrefix is table column name prefix for parent
	ParentPrefix    = "parent"
	configRoot      = "config_root"
	optional        = "optional"
	serviceProperty = "service"
)

const (
	definitionsInFile = "definitions"
	schemasInFile     = "schemas"
)

var sqlTypeMap = map[string]string{
	ObjectType:  "json",
	IntegerType: "bigint",
	UintType:    "numeric(21)",
	ArrayType:   "json",
	BooleanType: "bool",
	NumberType:  "float",
	StringType:  "varchar(255)",
	Base64Type:  "varchar(255)",
}

var sqlBindMap = map[string]string{
	ObjectType:  "json",
	IntegerType: "int",
	UintType:    "uint64",
	ArrayType:   "json",
	BooleanType: "bool",
	NumberType:  "float",
	StringType:  stringType,
	Base64Type:  stringType,
}

//API object has schemas and types for API definition.
type API struct {
	Schemas     []*Schema              `yaml:"schemas" json:"schemas,omitempty"`
	Definitions []*Schema              `yaml:"-" json:"-"`
	Overrides   []*Schema              `yaml:"-" json:"-"`
	Types       map[string]*JSONSchema `yaml:"-" json:"-"`
	Timestamp   time.Time

	SkipMissingRefs bool `yaml:"-" json:"-"`
}

//ColumnConfig is for database configuration.
type ColumnConfig struct {
	Path         string
	GetPath      string
	UpdatePath   string
	Bind         string
	Column       string
	ParentColumn []string
	Name         string
	JSONSchema   *JSONSchema
}

//ColumnConfigs is for list of columns
type ColumnConfigs []*ColumnConfig

func (c ColumnConfigs) Len() int {
	return len(c)
}
func (c ColumnConfigs) Swap(i, j int) {
	c[i], c[j] = c[j], c[i]
}

func (c ColumnConfigs) Less(i, j int) bool {
	return strings.Compare(
		strings.Join(c[i].ParentColumn, "")+c[i].Column,
		strings.Join(c[j].ParentColumn, "")+c[j].Column,
	) > 0
}

func (c ColumnConfigs) shortenColumn() {
	sort.Sort(c)
	if len(c) < 2 {
		return
	}
	for i := 0; i < len(c)-1; i++ {
		for j := i + 1; j < len(c); j++ {
			if c[j].Column == c[i].Column {
				c[j].Column = c[j].ParentColumn[len(c[j].ParentColumn)-1] + "_" + c[j].Column
				c[j].ParentColumn = c[j].ParentColumn[:len(c[j].ParentColumn)-1]
			}
		}
	}
}

//Schema represents a data model
type Schema struct {
	FileName              string                    `yaml:"-" json:"-"`
	ID                    string                    `yaml:"id" json:"id,omitempty"`
	Plural                string                    `yaml:"plural" json:"plural,omitempty"`
	Type                  string                    `yaml:"type" json:"type,omitempty"`
	Title                 string                    `yaml:"title" json:"title,omitempty"`
	Table                 string                    `yaml:"table" json:"table,omitempty"`
	Description           string                    `yaml:"description" json:"description,omitempty"`
	Parents               map[string]*Reference     `yaml:"parents" json:"parents,omitempty"`
	References            map[string]*Reference     `yaml:"references" json:"references,omitempty"`
	BackReferences        map[string]*BackReference `yaml:"-" json:"back_references,omitempty"`
	Prefix                string                    `yaml:"prefix" json:"prefix,omitempty"`
	JSONSchema            *JSONSchema               `yaml:"schema" json:"schema,omitempty"`
	Definitions           map[string]*JSONSchema    `yaml:"definitions" json:"-"`
	Extends               []string                  `yaml:"extends" json:"extends,omitempty"`
	Columns               ColumnConfigs             `yaml:"-" json:"-"`
	TypeName              string                    `yaml:"-" json:"-"`
	Path                  string                    `yaml:"-" json:"-"`
	PluralPath            string                    `yaml:"-" json:"-"`
	Children              map[string]*BackReference `yaml:"-" json:"-"`
	Index                 int                       `yaml:"-" json:"-"`
	ParentOptional        bool                      `yaml:"-" json:"-"`
	IsConfigRootInParents bool                      `yaml:"-" json:"-"`
	DefaultParent         *Reference                `yaml:"-" json:"-"`
}

//JSONSchema is a standard JSONSchema representation plus data for code generation.
type JSONSchema struct {
	ID                string                 `yaml:"-" json:"-"`
	Index             int                    `yaml:"-" json:"-"`
	Title             string                 `yaml:"title" json:"title,omitempty"`
	JSONTag           string                 `yaml:"json_tag" json:"-"`
	Description       string                 `yaml:"description" json:"description,omitempty"`
	SQL               string                 `yaml:"sql" json:"-"`
	Default           interface{}            `yaml:"default" json:"default,omitempty"`
	Operation         string                 `yaml:"operation" json:"-"`
	Presence          string                 `yaml:"presence" json:"presence,omitempty"`
	Type              string                 `yaml:"type" json:"type,omitempty"`
	Permission        []string               `yaml:"permission" json:"permission,omitempty"`
	Properties        map[string]*JSONSchema `yaml:"properties" json:"properties,omitempty"`
	OrderedProperties []*JSONSchema          `yaml:"-" json:"-"`
	Enum              []string               `yaml:"enum" json:"enum,omitempty"`
	Minimum           interface{}            `yaml:"minimum" json:"minimum,omitempty"`
	Maximum           interface{}            `yaml:"maximum" json:"maximum,omitempty"`
	Ref               string                 `yaml:"$ref" json:"-"`
	Items             *JSONSchema            `yaml:"items" json:"items,omitempty"`
	GoName            string                 `yaml:"-" json:"-"`
	GoType            string                 `yaml:"go_type" json:"go_type"`
	ProtoType         string                 `yaml:"proto_type" json:"proto_type"`
	Required          []string               `yaml:"required" json:"-"`
	GoPremitive       bool                   `yaml:"-" json:"-"`
	Format            string                 `yaml:"format" json:"format,omitempty"`

	// Properties relevant for collection types (with CollectionType == "map" or "list"):
	CollectionType string `yaml:"collectionType" json:"collectionType,omitempty"`
	// MapKey is name of MapKeyProperty.
	MapKey         string      `yaml:"mapKey" json:"mapKey,omitempty"`
	MapKeyProperty *JSONSchema `yaml:"mapKeyProperty" json:"mapKeyProperty,omitempty"`
}

//String makes string format for json schema.
func (s *JSONSchema) String() string {
	data, err := json.Marshal(s)
	if err != nil {
		logrus.WithError(err).Debug("Could not stringify JSONSchema")
	}
	return string(data)
}

// IsInt returns true if schema is of int type.
func (s *JSONSchema) IsInt() bool {
	return s.GoType == IntGoType
}

// IsUint returns true if schema is of int type.
func (s *JSONSchema) IsUint() bool {
	return s.GoType == UintGoType
}

// IsFloat returns true if schema is of float type.
func (s *JSONSchema) IsFloat() bool {
	return s.GoType == FloatGoType
}

// HasNumberFields returns true if JSONSchema has any number fields (int or float).
func (s *JSONSchema) HasNumberFields() bool {
	for _, property := range s.Properties {
		if property.IsInt() || property.IsFloat() || property.IsUint() {
			return true
		}
	}
	return false
}

//Reference object represents many to many relationships between resources.
type Reference struct {
	GoName      string        `yaml:"-" json:"-"`
	Table       string        `yaml:"-" json:"-"`
	Index       int           `yaml:"-" json:"-"`
	Description string        `yaml:"description" json:"description,omitempty"`
	Operations  string        `yaml:"operations" json:"operations,omitempty"`
	Presence    string        `yaml:"presence" json:"presence,omitempty"`
	Derived     bool          `yaml:"derived" json:"derived,omitempty"`
	RefType     string        `yaml:"-" json:"-"`
	Columns     ColumnConfigs `yaml:"-" json:"-"`
	Attr        *JSONSchema   `yaml:"attr" json:"attr"`
	LinkTo      *Schema       `yaml:"-" json:"-"`
	Ref         string        `yaml:"$ref" json:"$ref,omitempty"`
}

//BackReference for representing backward references.
type BackReference struct {
	Index       int     `yaml:"-" json:"-"`
	Description string  `yaml:"description" json:"description,omitempty"`
	LinkTo      *Schema `yaml:"-" json:"-"`
}

func parseRef(ref string) (file, section, goType string) {
	if ref == "" {
		return "", "", ""
	}
	refs := strings.Split(ref, "#")
	types := strings.Split(ref, "/")
	return refs[0], types[1], types[len(types)-1]
}

func (s *JSONSchema) getRefType() string {
	_, _, goType := parseRef(s.Ref)
	return goType
}

// Copy copies a json schema.
//
// Note that non pointer receiver is used to copy the object.
func (s JSONSchema) Copy() *JSONSchema {
	if s.Properties != nil {
		properties := map[string]*JSONSchema{}
		for name, property := range s.Properties {
			properties[name] = property.Copy()
		}
		s.Properties = properties
	}

	var op []*JSONSchema
	for _, p := range s.OrderedProperties {
		if prop := s.Properties[p.ID]; prop != nil {
			op = append(op, prop)
		}
	}
	s.OrderedProperties = op

	if s.Items != nil {
		s.Items = s.Items.Copy()
	}
	return &s
}

// Update merges two JSONSchema
// nolint: gocyclo
func (s *JSONSchema) Update(s2 *JSONSchema) {
	if s2 == nil {
		return
	}
	if s.Title == "" {
		s.Title = s2.Title
	}
	if s.JSONTag == "" {
		s.JSONTag = s2.JSONTag
	}
	if s.Description == "" {
		s.Description = s2.Description
	}
	if s.SQL == "" {
		s.SQL = s2.SQL
	}
	if s.Default == nil {
		s.Default = s2.Default
	}
	if s.Operation == "" {
		s.Operation = s2.Operation
	}
	if s.Format == "" {
		s.Format = s2.Format
	}
	if s.Presence == "" {
		s.Presence = s2.Presence
	}

	if s.Properties == nil && s2.Properties != nil {
		s.Properties = map[string]*JSONSchema{}
	}
	for name, property := range s2.Properties {
		s.Properties[name] = property.Copy()
	}
	s.Required = append(s2.Required, s.Required...)

	var props []*JSONSchema
	set := map[string]bool{}
	for _, p := range s2.OrderedProperties {
		id := p.ID
		if !set[id] {
			props = append(props, s.Properties[id])
			set[id] = true
		}
	}
	for _, p := range s.OrderedProperties {
		id := p.ID
		if !set[id] {
			props = append(props, s.Properties[p.ID])
			set[id] = true
		}
	}
	s.OrderedProperties = props
	if s.Type == "" {
		s.Type = s2.Type
	}
	if s.GoType == "" {
		s.GoType = s2.GoType
	}
	if s.ProtoType == "" {
		s.ProtoType = s2.ProtoType
	}
	if s.Enum == nil {
		s.Enum = s2.Enum
	}
	if s.Minimum == nil {
		s.Minimum = s2.Minimum
	}
	if s.Maximum == nil {
		s.Maximum = s2.Maximum
	}
	if s.Items == nil {
		s.Items = s2.Items
	}
}

//Walk apply one function for json schema recursively.
func (s *JSONSchema) Walk(do func(s2 *JSONSchema) error) error {
	if s == nil {
		return nil
	}
	err := do(s)
	if err != nil {
		return err
	}
	for _, property := range s.Properties {
		if err = property.Walk(do); err != nil {
			return err
		}
	}
	return nil
}

func (s *JSONSchema) resolveSQLForArray(
	parentColumn []string,
	columnName string,
	goPath string,
	getPath string,
	updatePath string,
	columns *ColumnConfigs,
) bool {
	if len(s.Properties) != 0 && s.Type != ArrayType {
		return false
	}

	if s.SQL == "" {
		s.SQL = sqlTypeMap[s.Type]
	}
	var bind string
	if s.GoType != "" {
		bind = sqlBindMap[s.Type]
	}

	*columns = append(*columns, &ColumnConfig{
		Path:         goPath,
		GetPath:      getPath,
		UpdatePath:   updatePath,
		Bind:         bind,
		Column:       strings.ToLower(columnName),
		ParentColumn: parentColumn,
		Name:         columnName,
		JSONSchema:   s,
	})

	return true
}

func (s *JSONSchema) resolveSQL(
	parentColumn []string,
	columnName string,
	goPath string,
	getPath string,
	updatePath string,
	columns *ColumnConfigs,
) error {
	if s == nil || s.Presence == serviceProperty {
		return nil
	}

	if s.resolveSQLForArray(parentColumn, columnName, goPath, getPath, updatePath, columns) {
		return nil
	}

	for name, property := range s.Properties {
		nextParentColumn := make([]string, len(parentColumn))
		copy(nextParentColumn, parentColumn)
		nextParentColumn = append(nextParentColumn, columnName)

		newUpdatePath := name
		if updatePath != "" {
			newUpdatePath = updatePath + "." + name
		}
		err := property.resolveSQL(nextParentColumn,
			name, goPath+"."+property.GoName,
			getPath+".Get"+property.GoName+"()", newUpdatePath, columns)
		if err != nil {
			return err
		}
	}
	return nil
}

// nolint: gocyclo
func (s *JSONSchema) resolveGoName(name string) error {
	if s == nil {
		return nil
	}
	s.GoName = format.SnakeToCamel(name)
	if s.GoName == "Size" {
		s.GoName = "Size_"
	}
	if s.ProtoType == "" {
		protoType := ""
		s.GoPremitive = true
		goType := ""
		switch s.Type {
		case IntegerType:
			goType = IntGoType
			protoType = IntProtoType
		case NumberType:
			goType = FloatGoType
			protoType = FloatProtoType
		case StringType:
			goType = stringType
			protoType = stringType
		case Base64Type:
			goType = "base64"
			protoType = stringType
		case BooleanType:
			goType = "bool"
			protoType = "bool"
		case ObjectType:
			goType = s.getRefType()
			if s.Properties == nil {
				goType = "map[string]interface{}"
			}

			if goType != "" {
				protoType = goType
			}
			if s.Properties == nil {
				protoType = "bytes"
			}
		case ArrayType:
			err := s.Items.resolveGoName(name)
			if err != nil {
				return err
			}
			if s.Items == nil {
				logrus.Errorf("Got <nil> Items for array in schema '%v': %+#v", name, s)
				goType = "[]string"
				protoType = "repeated string"
			} else {
				if s.Items.Type == IntegerType || s.Items.Type == NumberType || s.Items.Type == BooleanType ||
					s.Items.Type == StringType || s.Items.Type == ArrayType {
					goType = "[]" + s.Items.GoType
				} else {
					goType = "[]*" + s.Items.GoType
				}
				protoType = "repeated " + s.Items.ProtoType
			}
		}
		s.GoType = goType
		s.ProtoType = protoType
	}
	for pname, property := range s.Properties {
		err := property.resolveGoName(pname)
		if err != nil {
			return err
		}
	}
	return nil
}

func (api *API) definitionByFileName(fileName string) *Schema {
	for _, s := range api.Definitions {
		if s.FileName == fileName {
			return s
		}
	}
	return nil
}

//SchemaByID return schema by ID.
func (api *API) SchemaByID(id string) *Schema {
	for _, s := range api.Schemas {
		if s.ID == id {
			return s
		}
	}
	return nil
}

func (api *API) readDefinitionFromDefinitions(schemaFile, typeName string) (*JSONSchema, error) {
	definitions := api.definitionByFileName(schemaFile)
	if definitions == nil {
		logrus.Info("definitions read from following files:")
		for _, d := range api.Definitions {
			logrus.Info(d.FileName)
		}
		return nil, errors.Errorf("can't find file '%s' (with type %s)", schemaFile, typeName)
	}
	definition, ok := definitions.Definitions[typeName]
	if !ok {
		return nil, errors.Errorf("%s isn't defined in %s, please verify the definition is loaded", typeName, schemaFile)
	}
	return definition, nil
}

func (api *API) readDefinitionFromSchemas(typeName string) (*JSONSchema, error) {
	schema := api.SchemaByID(typeName)
	if schema == nil {
		return nil, errors.Errorf("can find schema with id: %v", typeName)
	}
	return schema.JSONSchema, nil
}

func (api *API) readDefinition(schemaFile, section, typeName string) (*JSONSchema, error) {
	switch section {
	case definitionsInFile:
		return api.readDefinitionFromDefinitions(schemaFile, typeName)
	case schemasInFile:
		return api.readDefinitionFromSchemas(typeName)
	}
	return nil, errors.Errorf("section '%v' not handled for reading definitions", section)
}

func (api *API) loadType(schemaFile, section, typeName string) (*JSONSchema, error) {
	if definition, ok := api.Types[typeName]; ok {
		return definition, nil
	}
	definition, err := api.readDefinition(schemaFile, section, typeName)
	if err != nil {
		return nil, errors.Wrapf(err, "reading definiton from %v", section)
	}
	err = definition.Walk(api.resolveRef)
	if err != nil {
		return nil, err
	}
	err = definition.resolveGoName("")
	if err != nil {
		return nil, err
	}
	err = definition.Walk(api.resolveJSONTag)
	if err != nil {
		return nil, err
	}
	api.Types[typeName] = definition
	return definition, nil
}

func (api *API) resolveRef(schema *JSONSchema) error {
	if schema == nil {
		return nil
	}
	if schema.Type == ArrayType {
		err := api.resolveRef(schema.Items)
		if err != nil {
			return err
		}
	}
	if schema.Ref == "" {
		return nil
	}
	definition, err := api.loadType(parseRef(schema.Ref))
	if err != nil {
		return errors.Wrapf(err, "resolve ref required by %v (ref: %v)", schema.ID, schema.Ref)
	}

	schema.Update(definition)
	return nil
}

func (api *API) resolveAllRef() error {
	for _, s := range api.Schemas {
		err := s.JSONSchema.Walk(api.resolveRef)
		if err != nil {
			return err
		}
	}
	return nil
}

func (api *API) resolveAllSQL() error {
	for _, s := range api.Schemas {
		err := s.JSONSchema.resolveSQL([]string{}, "", "", "", "", &s.Columns)
		if err != nil {
			return err
		}
		s.Columns.shortenColumn()
	}
	return nil
}

func (api *API) resolveRelation(linkToSchema *Schema, reference *Reference) error {
	linkTo := linkToSchema.ID
	reference.GoName = format.SnakeToCamel(linkTo)

	reference.LinkTo = linkToSchema
	ref := reference.Ref
	if ref == "" {
		return nil
	}
	file, section, jsonType := parseRef(ref)
	reference.RefType = jsonType
	definition, err := api.loadType(file, section, jsonType)
	if err != nil {
		return err
	}
	reference.Attr = definition
	err = definition.resolveGoName("")
	if err != nil {
		return err
	}
	return definition.resolveSQL([]string{}, "", "", "", "", &reference.Columns)
}

// UnmarshalYAML unmarshals Schema and sets default parent.
func (s *Schema) UnmarshalYAML(unmarshal func(i interface{}) error) (err error) {
	type Alias Schema
	if err = unmarshal((*Alias)(s)); err != nil {
		return err
	}
	if s.DefaultParent, err = s.getDefaultParent(unmarshal); err != nil {
		return err
	}
	return nil
}

func (s *Schema) getDefaultParent(unmarshal func(i interface{}) error) (*Reference, error) {
	p := struct {
		ParentsSlice yaml.MapSlice `yaml:"parents"`
	}{}
	if err := unmarshal(&p); err != nil {
		return nil, err
	}
	if len(p.ParentsSlice) > 0 {
		parentKey, ok := p.ParentsSlice[0].Key.(string)
		if !ok {
			return nil, errors.Errorf("invalid parent key: %v", p.ParentsSlice[0].Key)
		}
		return s.Parents[parentKey], nil
	}
	return nil, nil
}

// UnmarshalYAML unmarshals JSONSchema and preserves properties' order.
func (s *JSONSchema) UnmarshalYAML(unmarshal func(i interface{}) error) (err error) {
	type Alias JSONSchema
	a := struct {
		Alias `yaml:",inline"`
	}{}
	if err = unmarshal(&a); err != nil {
		return err
	}
	*s = JSONSchema(a.Alias)
	var op []*JSONSchema
	if op, err = s.makeOrderedProperties(unmarshal); err != nil {
		return err
	}
	for id, p := range s.Properties {
		p.ID = id
	}
	s.OrderedProperties = op
	s.applyOverridenTypes()
	return nil
}

func (s *JSONSchema) makeOrderedProperties(unmarshal func(i interface{}) error) ([]*JSONSchema, error) {
	p := struct {
		PropertiesSlice yaml.MapSlice `yaml:"properties"`
	}{}
	if err := unmarshal(&p); err != nil {
		return nil, err
	}
	var op []*JSONSchema
	for _, p := range p.PropertiesSlice {
		key, ok := p.Key.(string)
		if !ok {
			return nil, errors.Errorf("got invalid property key, string expected, got: %+v", p.Key)
		}
		op = append(op, s.Properties[key])
	}
	return op, nil
}

var overriddenTypes = map[string]struct{}{
	"types.json#/definitions/AccessType":       {},
	"types.json#/definitions/L4PortType":       {},
	"types.json#/definitions/IpAddressType":    {},
	"types.json#/definitions/MacAddressesType": {},
}

func (s *JSONSchema) applyOverridenTypes() {
	if s == nil {
		return
	}
	for key, property := range s.Properties {
		// TODO: remove this workaround when js is updated for zero-value required properties
		_, present := overriddenTypes[property.Ref]

		if (present || property.Type == "boolean") &&
			(property.Presence == "required" || property.Presence == "true") {
			logrus.WithField("schema", property).Warnf(
				"Property '%s' should be optional as it may have zero-value. Update schema.", key,
			)
			property.Presence = "optional"
		}
	}
}

func makeShort(id string) string {
	id = strings.Replace(id, "virtual", "v", -1)
	id = strings.Replace(id, "network", "net", -1)
	id = strings.Replace(id, "interface", "i", -1)
	id = strings.Replace(id, "machine", "m", -1)
	id = strings.Replace(id, "router", "r", -1)
	id = strings.Replace(id, "structured_syslog", "log", -1)
	return id
}

//ReferenceTableName make reference table name.
func ReferenceTableName(prefix, id, linkTo string) string {
	table := prefix + "_" + id + "_" + linkTo
	if len(table) < maxColumnLen {
		return strings.ToLower(table)
	}
	return strings.ToLower(prefix + "_" + makeShort(id) + "_" + makeShort(linkTo))
}

// ChildColumnName makes child column name.
func ChildColumnName(childSchemaID, schemaID string) string {
	return strings.ToLower("child_" + childSchemaID + "_" + schemaID)
}

// BackRefColumnName makes back reference column name.
func BackRefColumnName(fromID, toID string) string {
	return strings.ToLower("backref_" + fromID + "_" + toID)
}

// nolint: gocyclo
func (api *API) resolveAllRelation() error {
	for _, s := range api.Schemas {
		if s.Type == AbstractType {
			continue
		}
		refs := make(map[string]*Reference)
		for linkTo, reference := range s.References {
			reference.Table = ReferenceTableName(RefPrefix, s.Table, linkTo)
			referenceSchema, err := api.resolveReference(s, reference, linkTo)
			if err != nil {
				if !api.SkipMissingRefs {
					return errors.Wrap(err, "error resolving reference")
				}
				logrus.Warnf("Skipping reference '%s' (--skip-missing-refs), error: %v", linkTo, err)
				continue
			}
			refs[linkTo] = reference
			referenceSchema.BackReferences[s.ID] = &BackReference{LinkTo: s, Description: reference.Description}
		}
		s.References = refs
		_, ok := s.Parents[configRoot]
		if ok {
			s.IsConfigRootInParents = true
			delete(s.Parents, configRoot)
		}
		for linkTo, parent := range s.Parents {
			if parent.Presence == optional {
				s.ParentOptional = true
			}
			s.DefaultParent = parent
			parent.Table = ReferenceTableName(ParentPrefix, s.Table, linkTo)
			parentSchema, err := api.resolveReference(s, parent, linkTo)
			if err != nil {
				return errors.Wrap(err, "error resolving parent reference")
			}
			parentSchema.Children[s.ID] = &BackReference{LinkTo: s, Description: parent.Description}
		}
	}
	return nil
}

// HasParents returns whether schema has any parent.
func (s *Schema) HasParents() bool {
	return len(s.Parents) > 0
}

func (api *API) resolveReference(from *Schema, to *Reference, toID string) (*Schema, error) {
	linkToSchema := api.SchemaByID(toID)
	if linkToSchema == nil {
		return nil, errors.Errorf("missing linked schema '%s' for reference '%v' in schema %v [%v]",
			toID, toID, from.ID, from.FileName)
	}
	if err := api.resolveRelation(linkToSchema, to); err != nil {
		return nil, err
	}
	return linkToSchema, nil
}

func (api *API) resolveIndex() error {
	schemaIndex := schemaStartIndex
	for _, s := range api.Schemas {
		if s.Type == AbstractType {
			continue
		}
		s.Index = schemaIndex
		schemaIndex += schemaIndexOffset
		index := 1
		for _, property := range s.JSONSchema.OrderedProperties {
			property.Index = index
			index++
		}
		index = index + propertyIndexOffset
		for _, key := range sortedReferenceIDs(s.References) {
			reference := s.References[key]
			reference.Index = index
			index++
		}
		index = index + propertyIndexOffset
		for _, backReference := range s.Children {
			backReference.Index = index
			index++
		}
		index = index + propertyIndexOffset
		for _, backReference := range s.BackReferences {
			backReference.Index = index
			index++
		}
	}
	for _, t := range api.Types {
		for index, property := range t.OrderedProperties {
			property.Index = index + 1
		}
	}
	return nil
}

func sortedReferenceIDs(refs map[string]*Reference) []string {
	var keys []string
	for k := range refs {
		keys = append(keys, k)
	}
	sort.Strings(keys)
	return keys
}

func (api *API) resolveAllGoName() error {
	for _, s := range api.Schemas {
		err := s.JSONSchema.resolveGoName(s.ID)
		if err != nil {
			return err
		}
	}
	return nil
}

func (api *API) resolveExtend() error {
	for _, s := range api.Schemas {
		for _, baseSchemaID := range s.Extends {
			baseSchema := api.SchemaByID(baseSchemaID)
			s.Extend(baseSchema)
		}
	}
	return nil
}

// Extend extends schema, references and parents fields with values present in `by`.
func (s *Schema) Extend(by *Schema) {
	if s == nil || by == nil {
		return
	}
	s.JSONSchema.Update(by.JSONSchema)
	s.extendReferences(by)
	s.extendParents(by)
}

func (s *Schema) extendReferences(by *Schema) {
	if s.References == nil {
		s.References = map[string]*Reference{}
	}
	for k, v := range by.References {
		c := *v
		s.References[k] = &c
	}
}

func (s *Schema) extendParents(by *Schema) {
	if s.Parents == nil {
		s.Parents = map[string]*Reference{}
	}
	for k, v := range by.Parents {
		c := *v
		s.Parents[k] = &c
	}
}

func (api *API) resolveAllJSONTag() error {
	for _, schema := range api.Schemas {
		err := schema.JSONSchema.Walk(api.resolveJSONTag)
		if err != nil {
			return err
		}
	}
	return nil
}

func (api *API) resolveJSONTag(schema *JSONSchema) error {
	if schema == nil {
		return nil
	}
	if len(schema.JSONTag) == 0 {
		schema.JSONTag = schema.ID
	}

	for _, prop := range schema.Properties {
		if len(prop.JSONTag) == 0 {
			prop.JSONTag = prop.ID
		}
	}
	return nil
}

func (api *API) resolveCollectionTypes() error {
	for _, s := range api.Schemas {
		for propertyName, property := range s.JSONSchema.Properties {
			if property.CollectionType != "" {
				propertyType := api.Types[property.ProtoType]

				if err := checkCollectionTypes(property, propertyType); err != nil {
					return err
				}
				propertyType.CollectionType = property.CollectionType

				if propertyType.CollectionType == "map" {
					if err := resolveMapCollectionType(property, propertyType); err != nil {
						return errors.Wrapf(err, "invalid %q property of %q schema", propertyName, s.ID)
					}
				}
			}
		}
	}
	return nil
}

func checkCollectionTypes(property, propertyType *JSONSchema) error {
	if propertyType.CollectionType != "" && propertyType.CollectionType != property.CollectionType {
		return errors.Errorf(
			"type %v is used as multiple collection types - %v and %v",
			property.ProtoType, property.CollectionType, propertyType.CollectionType,
		)
	}
	return nil
}

func resolveMapCollectionType(property, propertyType *JSONSchema) error {
	if property.MapKey == "" {
		return errors.New("empty mapKey field")
	}

	propertyType.MapKeyProperty = propertyType.OrderedProperties[0].Items.Properties[property.MapKey]
	return nil
}

// MakeAPI load directory and generate API definitions.
func MakeAPI(paths, addons []string, skipMissingRefs bool) (*API, error) {
	api := &API{
		Schemas:         []*Schema{},
		Definitions:     []*Schema{},
		Types:           map[string]*JSONSchema{},
		SkipMissingRefs: skipMissingRefs,
	}
	logrus.Printf("Making API from schema paths: %v", paths)
	for _, path := range paths {

		if err := filepath.Walk(path, api.walkSchemaFile); err != nil {
			return nil, errors.Wrap(err, "error walking schema directory")
		}

	}
	for _, addon := range addons {
		overrides, err := api.readOverrides(addon)
		if err != nil {
			return nil, err
		}
		api.Overrides = append(api.Overrides, overrides...)
	}

	err := api.process()
	return api, err
}

func (api *API) readOverrides(dir string) ([]*Schema, error) {
	var result []*Schema
	if err := walkOverFilesIfDirExists(dir, func(path string, f os.FileInfo) error {
		logrus.Infof("Reading addons from %v file", path)
		schema, err := api.loadSchemaFromPath(path)
		if err != nil {
			return err
		}
		result = append(result, &schema)
		return nil
	}); err != nil {
		return nil, errors.Wrap(err, "error walking overrides directory")
	}
	return result, nil
}

func walkOverFilesIfDirExists(dir string, do func(path string, f os.FileInfo) error) error {
	err := filepath.Walk(dir, func(path string, f os.FileInfo, err error) error {
		if f == nil || f.IsDir() || err != nil {
			return err
		}
		return do(path, f)
	})
	if os.IsNotExist(err) || isNotDir(err) {
		return nil // Silence not-exist and not a dir errors
	}
	return err
}

func isNotDir(err error) bool {
	p, ok := err.(*os.PathError)
	return ok && p != nil && p.Err == syscall.ENOTDIR
}

func (api *API) loadSchemaFromPath(path string) (Schema, error) {
	schema, modtime, err := readSchemaFromPath(path)
	if err != nil {
		return Schema{}, err
	}
	if modtime.After(api.Timestamp) {
		api.Timestamp = modtime
	}

	return schema, nil
}

func readSchemaFromPath(path string) (Schema, time.Time, error) {
	var schema Schema
	err := fileutil.LoadFile(path, &schema)
	if err != nil {
		return Schema{}, time.Time{}, errors.Wrapf(err, "failed to load file %s", path)
	}
	info, err := os.Stat(path)
	if err != nil {
		return Schema{}, time.Time{}, errors.Wrapf(err, "failed to stat file %s", path)
	}
	logrus.Debugf("Loading schema from %v - %v", path, schema.ID)
	return schema, info.ModTime(), nil
}

func (api *API) walkSchemaFile(path string, f os.FileInfo, err error) error {
	if f == nil || f.IsDir() || err != nil {
		return err
	}
	schema, err := api.loadSchemaFromPath(path)
	if err != nil {
		return err
	}

	r := strings.NewReplacer(".yml", ".json", ".yaml", ".json")
	schema.FileName = r.Replace(filepath.Base(path))
	return api.processSchema(&schema)
}

func (api *API) processSchema(schema *Schema) error {
	schema.JSONSchema.applyOverridenTypes()
	schema.TypeName = strings.Replace(schema.ID, "_", "-", -1)
	if schema.Table == "" {
		schema.Table = strings.ToLower(schema.ID)
	}
	schema.Path = schema.TypeName
	schema.PluralPath = strings.Replace(schema.Plural, "_", "-", -1)
	schema.BackReferences = map[string]*BackReference{}
	schema.Children = map[string]*BackReference{}
	if schema.ID != "" {
		api.Schemas = append(api.Schemas, schema)
	}
	if len(schema.Definitions) > 0 {
		api.Definitions = append(api.Definitions, schema)
	}
	return nil
}

func (api *API) process() error {
	if err := api.resolveOverrides(); err != nil {
		return err
	}
	if err := api.resolveAllRef(); err != nil {
		return err
	}
	if err := api.resolveExtend(); err != nil {
		return err
	}
	if err := api.resolveAllGoName(); err != nil {
		return err
	}
	if err := api.resolveAllSQL(); err != nil {
		return err
	}
	if err := api.resolveAllRelation(); err != nil {
		return err
	}
	if err := api.resolveIndex(); err != nil {
		return err
	}
	if err := api.resolveCollectionTypes(); err != nil {
		return err
	}
	err := api.resolveAllJSONTag()
	return err
}

func (api *API) resolveOverrides() error {
	for _, s := range api.Overrides {
		for key, def := range s.Definitions {
			for _, defSchema := range api.Definitions {
				defSchema.Definitions[key] = def
			}
		}
		if s.ID != "" {
			toChange := api.SchemaByID(s.ID)
			toChange.Extend(s)
		}
	}
	return nil
}
