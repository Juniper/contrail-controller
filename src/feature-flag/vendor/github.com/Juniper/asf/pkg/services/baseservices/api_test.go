package baseservices

import (
	"testing"

	"github.com/stretchr/testify/assert"
)

func TestParseFilter(t *testing.T) {
	filter := parseFilter("check==a,check==b,name==Bob")
	assert.Equal(t, []*Filter{
		{
			Key:    "check",
			Values: []string{"a", "b"},
		},
		{
			Key:    "name",
			Values: []string{"Bob"},
		},
	}, filter, "parse filter correctly")
}

func TestEncodeFilter(t *testing.T) {
	filterString := encodeFilter([]*Filter{
		{
			Key:    "check",
			Values: []string{"a", "b"},
		},
		{
			Key:    "name",
			Values: []string{"Bob"},
		},
	})
	assert.Equal(t, "check==a,check==b,name==Bob", filterString)
}

func TestEncodeNilFilter(t *testing.T) {
	filterString := encodeFilter(nil)
	assert.Equal(t, "", filterString)
}

func TestEmptyURLQuerySpec(t *testing.T) {
	spec := &ListSpec{}
	assert.Equal(t, "", spec.URLQuery().Encode())
}

func TestNilURLQuerySpec(t *testing.T) {
	var spec *ListSpec
	assert.Equal(t, "", spec.URLQuery().Encode())
}

func TestURLQuerySpec(t *testing.T) {
	spec := &ListSpec{
		Limit:  10,
		Marker: "c6b11cbf-f225-4c9b-b61c-b892a7e66747",
		Detail: true,
		Filters: []*Filter{
			{
				Key:    "check",
				Values: []string{"a", "b"},
			},
			{
				Key:    "name",
				Values: []string{"Bob"},
			},
		},
		Fields:       []string{"a", "b", "c"},
		ParentType:   "test",
		ParentFQName: []string{"a", "b"},
		Count:        true,
		ExcludeHrefs: true,
		Shared:       true,
		ParentUUIDs:  []string{"a", "b"},
		BackRefUUIDs: []string{"a", "b"},
		ObjectUUIDs:  []string{"a", "b"},
	}
	assert.Equal(
		t,
		"back_ref_id=a%2Cb&count=true&detail=true&exclude_hrefs=true"+
			"&fields=a%2Cb%2Cc&filters=check%3D%3Da%2Ccheck%3D%3Db%2Cname%3D%3DBob"+
			"&obj_uuids=a%2Cb&page_limit=10&page_marker=c6b11cbf-f225-4c9b-b61c-b892a7e66747"+
			"&parent_fq_name_str=a%3Ab&parent_id=a%2Cb&parent_type=test&shared=true",
		spec.URLQuery().Encode(),
	)
}
