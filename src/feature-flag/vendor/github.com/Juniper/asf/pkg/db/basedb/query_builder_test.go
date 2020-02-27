package basedb

import (
	"testing"

	"github.com/stretchr/testify/assert"

	"github.com/Juniper/asf/pkg/services/baseservices"
)

type queryBuilderParams struct {
	table         string
	fields        []string
	refFields     map[string][]string
	childFields   map[string][]string
	backRefFields map[string][]string
}

func TestQueryBuilder(t *testing.T) {
	type expectedResult struct {
		query  string
		values []interface{}
	}

	tests := []struct {
		name               string
		queryBuilderParams queryBuilderParams
		spec               baseservices.ListSpec
		expected           expectedResult
	}{
		{
			name: "Collection of alarms",
			queryBuilderParams: queryBuilderParams{
				fields: []string{
					"global_access",
					"group_access",
				},
				table: "alert",
			},
			spec: baseservices.ListSpec{},
			expected: expectedResult{
				query: "select \"alert_t\".\"global_access\",\"alert_t\".\"group_access\" from alert " +
					"as alert_t order by \"alert_t\".\"uuid\"",
				values: []interface{}{},
			},
		},
		{
			name: "Collection of virtual networks are limited",
			queryBuilderParams: queryBuilderParams{
				fields: []string{
					"uuid",
					"name",
				},
				table: "virtual_network",
			},
			spec: baseservices.ListSpec{
				Limit: 10,
			},
			expected: expectedResult{
				query: "select \"virtual_network_t\".\"uuid\",\"virtual_network_t\".\"name\" from virtual_network " +
					"as virtual_network_t order by \"virtual_network_t\".\"uuid\" limit 10",
				values: []interface{}{},
			},
		},
		{
			name: "Test one ref filter from three ref fields",
			queryBuilderParams: queryBuilderParams{
				fields: []string{
					"uuid",
					"name",
				},
				refFields: map[string][]string{
					"project": nil,
					"vmi":     nil,
					"tag":     nil,
				},
				table: "floating_ip",
			},
			spec: baseservices.ListSpec{
				RefUUIDs: map[string]*baseservices.UUIDs{
					"project_refs": {UUIDs: []string{"proj_ref_uuid", "proj_ref_uuid_2"}},
				},
			},
			expected: expectedResult{
				query: "select \"floating_ip_t\".\"uuid\",\"floating_ip_t\".\"name\" from floating_ip as floating_ip_t " +
					"left join \"ref_floating_ip_project\" on \"floating_ip_t\".\"uuid\" = \"ref_floating_ip_project\".\"from\" " +
					"where (\"ref_floating_ip_project\".\"to\" :: TEXT in ($1,$2)) order by \"floating_ip_t\".\"uuid\"",
				values: []interface{}{
					"proj_ref_uuid",
					"proj_ref_uuid_2",
				},
			},
		},
		{
			name: "Collecion of virtual networks are limited and started with marker",
			queryBuilderParams: queryBuilderParams{
				fields: []string{
					"parent_type",
					"parent_uuid",
				},
				table: "virtual_network",
			},
			spec: baseservices.ListSpec{
				Limit:  5,
				Marker: "marker_uuid",
			},
			expected: expectedResult{
				query: "select \"virtual_network_t\".\"parent_type\",\"virtual_network_t\".\"parent_uuid\" from virtual_network " +
					"as virtual_network_t where \"virtual_network_t\".\"uuid\" > $1 order by \"virtual_network_t\".\"uuid\" limit 5",
				values: []interface{}{
					"marker_uuid",
				},
			},
		},
		{
			name: "Filtering virtual networks through ParentFQName",
			queryBuilderParams: queryBuilderParams{
				fields: []string{
					"fq_name",
				},
				table: "virtual_network",
			},
			spec: baseservices.ListSpec{
				ParentFQName: []string{"domain", "project"},
			},
			expected: expectedResult{
				query: "select \"virtual_network_t\".\"fq_name\" from virtual_network as virtual_network_t " +
					"where \"virtual_network_t\".\"fq_name\" ->> 0 = $1 and \"virtual_network_t\".\"fq_name\" ->> 1 = $2 " +
					"and json_array_length(\"virtual_network_t\".\"fq_name\") = 3 order by \"virtual_network_t\".\"uuid\"",
				values: []interface{}{
					"domain",
					"project",
				},
			},
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			query, _, values := newQueryBuilder(tt.queryBuilderParams).ListQuery(nil, &tt.spec)

			assert.Equal(t, tt.expected.query, query)
			assert.Equal(t, tt.expected.values, values)

		})
	}
}

func TestRelaxRefQuery(t *testing.T) {
	tests := []struct {
		name               string
		queryBuilderParams queryBuilderParams
		linkTo             string
		expected           string
	}{
		{
			name: "Reference from VirtualNetwork to NetworkPolicy",
			queryBuilderParams: queryBuilderParams{
				table: "virtual_network",
			},
			linkTo:   "network_policy",
			expected: `update ref_virtual_network_network_policy set "relaxed" = true where "from" = $1 and "to" = $2`,
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			query := newQueryBuilder(tt.queryBuilderParams).RelaxRefQuery(tt.linkTo)

			assert.Equal(t, tt.expected, query)
		})
	}
}

func TestDeleteRelaxedBackrefsQuery(t *testing.T) {
	tests := []struct {
		name               string
		queryBuilderParams queryBuilderParams
		linkFrom           string
		expected           string
	}{
		{
			name:     "References from VirtualNetwork to NetworkPolicy",
			linkFrom: "virtual_network",
			queryBuilderParams: queryBuilderParams{
				table: "network_policy",
			},
			expected: `delete from ref_virtual_network_network_policy where "to" = $1 and "relaxed" = true`,
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			query := newQueryBuilder(tt.queryBuilderParams).DeleteRelaxedBackrefsQuery(tt.linkFrom)

			assert.Equal(t, tt.expected, query)
		})
	}
}

func newQueryBuilder(p queryBuilderParams) *QueryBuilder {
	return NewQueryBuilder(
		NewDialect(),
		p.table,
		p.fields,
		p.refFields,
		p.childFields,
		p.backRefFields)
}
