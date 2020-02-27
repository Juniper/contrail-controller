package schema

import (
	"testing"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
)

func TestSchema(t *testing.T) {
	api, err := MakeAPI([]string{"test_data/simple/schema"}, []string{"test_data/simple/overrides"}, false)
	assert.Nil(t, err, "API reading failed")
	assert.Len(t, api.Types, 4)
	assert.Len(t, api.Schemas, 4)
	project := api.SchemaByID("project")

	assert.Equal(t, "project", project.Table)
	assert.Len(t, project.JSONSchema.Properties, 4)
	assert.Len(t, project.JSONSchema.OrderedProperties, 4)
	assert.Len(t, project.Columns, 4)
	uuidProp := project.JSONSchema.Properties["uuid"]
	if assert.NotNil(t, uuidProp) {
		assert.Equal(t, "CR", uuidProp.Operation)
	}

	virtualNetwork := api.SchemaByID("virtual_network")
	assert.Equal(t, "vn", virtualNetwork.Table)
	assert.Len(t, virtualNetwork.JSONSchema.Properties, 4)
	assert.Equal(t, "uint64", virtualNetwork.JSONSchema.Properties["version"].GoType)
	for i, id := range []string{"uuid", "version", "display_name", "virtual_network_network_id"} {
		assert.Equal(t, id, virtualNetwork.JSONSchema.OrderedProperties[i].ID)
	}
	assert.Equal(t, 4, len(virtualNetwork.Columns))
	assert.Equal(t, 1005, virtualNetwork.References["network_ipam"].Index)
}

func TestMakeAPIGivenInvalidRef(t *testing.T) {
	tests := map[string]struct {
		skipMissingRefs bool
		fails           bool
	}{
		"skipMissingRefs unset":       {skipMissingRefs: false, fails: true},
		"skipMissingRefs set to true": {skipMissingRefs: true},
	}

	for name, tt := range tests {
		t.Run(name, func(t *testing.T) {
			api, err := MakeAPI([]string{"test_data/invalid_ref"}, nil, tt.skipMissingRefs)
			if tt.fails {
				assert.Error(t, err)
			} else {
				assert.NoError(t, err)

				assert.Len(t, api.Types, 1)
				assert.Len(t, api.Schemas, 3)

				virtualNetwork := api.SchemaByID("virtual_network")
				assert.Nil(t, virtualNetwork.References["network_ipam"])
			}
		})
	}
}

func TestSchemaEnums(t *testing.T) {
	api, err := MakeAPI([]string{"test_data/schema_enums/schema"}, []string{"test_data/schema_enums/overrides"}, false)
	assert.Nil(t, err, "API reading failed")
	project := api.SchemaByID("project")
	assert.NotNil(t, project, "Project can't be <nil>")
	obj := api.SchemaByID("simple_object")
	assert.NotNil(t, obj, "SimpleObject can't be <nil>")
	// In addition 'uuid' and 'display_name' are added (+2)
	assert.Equal(t, 3+2, len(obj.JSONSchema.Properties))

	assert.NotNil(t, api.Types)
	assert.Equal(t, 4, len(api.Types))
	enumArr, ok := api.Types["ObjectThatReferencesEnumAsArray"]
	assert.True(t, ok)
	checkPropertyRepeated(t, enumArr)
	enumArrOvrd, ok := api.Types["ObjectThatReferencesEnumAsArrayOverriden"]
	assert.True(t, ok)
	checkPropertyRepeated(t, enumArrOvrd)
}

func TestReferencesExtendBase(t *testing.T) {
	api, err := MakeAPI([]string{"test_data/schema_extend"}, nil, false)
	require.Nil(t, err, "API reading failed")
	assert.Equal(t, 5, len(api.Schemas))

	base := api.SchemaByID("base")
	require.NotNil(t, base, "Base object can't be <nil>")
	assert.Equal(t, 1, len(base.References))

	zeroRefObj := api.SchemaByID("derived_object")
	require.NotNil(t, zeroRefObj, "derived_object schema shouldn't be <nil>")
	assert.Equal(t, 1, len(zeroRefObj.References))

	ownRefObj := api.SchemaByID("derived_own_refs_object")
	require.NotNil(t, ownRefObj, "derived_own_refs_object schema shouldn't be <nil>")
	assert.Equal(t, 2, len(ownRefObj.References))
}

func TestJSONTag(t *testing.T) {
	api, err := MakeAPI([]string{"test_data/schema_extend"}, nil, false)
	require.Nil(t, err, "API reading failed")
	assert.Equal(t, 5, len(api.Schemas))

	base := api.SchemaByID("base")
	require.NotNil(t, base, "Base object can't be <nil>")

	assert.Equal(t, "colon:separated:in:base", base.JSONSchema.Properties["colonseparatedinbase"].JSONTag)

	derived := api.SchemaByID("derived_object")
	require.NotNil(t, derived, "derived_object schema shouldn't be <nil>")
	assert.Equal(t, "colon:separated:in:derived", derived.JSONSchema.Properties["colonseparatedinderived"].JSONTag)
}

func checkPropertyRepeated(t *testing.T, obj *JSONSchema) {
	assert.NotNil(t, obj)
	assert.Len(t, obj.Properties, 1)
	assert.Equal(t, obj.Properties[obj.OrderedProperties[0].ID].Type, ArrayType)

	if assert.NotNil(t, obj.Properties[obj.OrderedProperties[0].ID].Items) {
		assert.Equal(t, obj.Properties[obj.OrderedProperties[0].ID].Items.Type, StringType)
	}
}

func TestReferenceTableName(t *testing.T) {
	assert.Equal(
		t,
		"ref__v_net_i_v_net_i_v_net_i_v_net_i_v_net_i",
		ReferenceTableName("ref_", "virtual_network_interface_virtual_network_interface",
			"virtual_network_interface_virtual_network_interface_virtual_network_interface"))
}

func TestJSONSchemaUpdate(t *testing.T) {
	type m map[string]*JSONSchema

	tests := []struct {
		name           string
		a, b, expected *JSONSchema
	}{{
		name: "nil",
	}, {
		name:     "type is updated when empty",
		a:        &JSONSchema{},
		b:        &JSONSchema{Type: "type"},
		expected: &JSONSchema{Type: "type"},
	}, {
		name:     "type is not updated when set",
		a:        &JSONSchema{Type: "old"},
		b:        &JSONSchema{Type: "type"},
		expected: &JSONSchema{Type: "old"},
	}, {
		name: "properties are merged",
		a:    &JSONSchema{Properties: m{"propa": strProp("propa")}, OrderedProperties: []*JSONSchema{strProp("propa")}},
		b:    &JSONSchema{Properties: m{"propb": intProp("propb")}, OrderedProperties: []*JSONSchema{intProp("propb")}},
		expected: &JSONSchema{
			Properties:        m{"propa": strProp("propa"), "propb": intProp("propb")},
			OrderedProperties: []*JSONSchema{intProp("propb"), strProp("propa")},
		},
	}, {
		name:     "duplicated properies are overwritten",
		a:        &JSONSchema{Properties: m{"prop": strProp("prop")}, OrderedProperties: []*JSONSchema{strProp("prop")}},
		b:        &JSONSchema{Properties: m{"prop": intProp("prop")}, OrderedProperties: []*JSONSchema{intProp("prop")}},
		expected: &JSONSchema{Properties: m{"prop": intProp("prop")}, OrderedProperties: []*JSONSchema{intProp("prop")}},
	}}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			tt.a.Update(tt.b)
			assert.Equal(t, tt.expected, tt.a)
		})
	}
}

func prop(id, typ string) *JSONSchema {
	return &JSONSchema{ID: id, Type: typ}
}

func strProp(id string) *JSONSchema {
	return prop(id, StringType)
}

func intProp(id string) *JSONSchema {
	return prop(id, IntegerType)
}
