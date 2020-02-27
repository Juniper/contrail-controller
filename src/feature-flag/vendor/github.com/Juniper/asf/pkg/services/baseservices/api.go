package baseservices

import (
	"net/url"
	"strconv"
	"strings"

	"github.com/labstack/echo"

	"github.com/Juniper/asf/pkg/models/basemodels"
)

// Block of constants consumed by client code.
const (
	FiltersKey      = "filters"
	PageLimitKey    = "page_limit"
	PageMarkerKey   = "page_marker"
	DetailKey       = "detail"
	CountKey        = "count"
	SharedKey       = "shared"
	ExcludeHRefsKey = "exclude_hrefs"
	ParentFQNameKey = "parent_fq_name_str"
	ParentTypeKey   = "parent_type"
	ParentUUIDsKey  = "parent_id"
	BackrefUUIDsKey = "back_ref_id"
	ObjectUUIDsKey  = "obj_uuids"
	FieldsKey       = "fields"
)

func parsePositiveNumber(query string, defaultValue int64) int64 {
	i, err := strconv.Atoi(query)
	if err != nil {
		return defaultValue
	}
	if i < 0 {
		return defaultValue
	}
	return int64(i)
}

func parseBool(query string) bool {
	return strings.ToLower(query) == "true"
}

func parseStringList(query string) []string {
	if query == "" {
		return nil
	}
	return strings.Split(query, ",")
}

// GetListSpec makes ListSpec from query parameters.
func GetListSpec(c echo.Context) *ListSpec {
	return &ListSpec{
		Filters:      parseFilter(c.QueryParam(FiltersKey)),
		Limit:        parsePositiveNumber(c.QueryParam(PageLimitKey), 0),
		Marker:       c.QueryParam(PageMarkerKey),
		Detail:       parseBool(c.QueryParam(DetailKey)),
		Count:        parseBool(c.QueryParam(CountKey)),
		Shared:       parseBool(c.QueryParam(SharedKey)),
		ExcludeHrefs: parseBool(c.QueryParam(ExcludeHRefsKey)),
		ParentFQName: basemodels.ParseFQName(c.QueryParam(ParentFQNameKey)),
		ParentType:   c.QueryParam(ParentTypeKey),
		ParentUUIDs:  parseStringList(c.QueryParam(ParentUUIDsKey)),
		BackRefUUIDs: parseStringList(c.QueryParam(BackrefUUIDsKey)),
		// TODO(Daniel): handle RefUUIDs
		ObjectUUIDs: parseStringList(c.QueryParam(ObjectUUIDsKey)),
		Fields:      parseStringList(c.QueryParam(FieldsKey)),
	}
}

// URLQuery returns URL query strings.
func (s *ListSpec) URLQuery() url.Values {
	if s == nil {
		return nil
	}

	query := url.Values{}
	addQuery(query, FiltersKey, encodeFilter(s.Filters))
	if s.Limit > 0 {
		addQuery(query, PageLimitKey, strconv.FormatInt(s.Limit, 10))
	}
	if s.Marker != "" {
		addQuery(query, PageMarkerKey, s.Marker)
	}
	addQueryBool(query, DetailKey, s.Detail)
	addQueryBool(query, CountKey, s.Count)
	addQueryBool(query, SharedKey, s.Shared)
	addQueryBool(query, ExcludeHRefsKey, s.ExcludeHrefs)
	addQuery(query, ParentTypeKey, s.ParentType)
	addQuery(query, ParentFQNameKey, basemodels.FQNameToString(s.ParentFQName))
	addQuery(query, ParentUUIDsKey, encodeStringList(s.ParentUUIDs))
	addQuery(query, BackrefUUIDsKey, encodeStringList(s.BackRefUUIDs))
	// TODO(Daniel): handle RefUUIDs
	addQuery(query, ObjectUUIDsKey, encodeStringList(s.ObjectUUIDs))
	addQuery(query, FieldsKey, encodeStringList(s.Fields))
	return query
}

func addQuery(query url.Values, key, value string) {
	if value != "" {
		query.Add(key, value)
	}
}

func addQueryBool(query url.Values, key string, value bool) {
	if value {
		query.Add(key, strconv.FormatBool(value))
	}
}

// AppendFilter return a filter for specific key.
func AppendFilter(filters []*Filter, key string, values ...string) []*Filter {
	var filter *Filter
	if len(values) == 0 {
		return filters
	}
	for _, f := range filters {
		if f.Key == key {
			filter = f
			break
		}
	}
	if filter == nil {
		filter = &Filter{
			Key:    key,
			Values: []string{},
		}
		filters = append(filters, filter)
	}
	filter.Values = append(filter.Values, values...)
	return filters
}

// QueryString returns string for query string.
func (f *Filter) QueryString() string {
	var sl []string
	for _, value := range f.Values {
		sl = append(sl, f.Key+"=="+value)
	}
	return encodeStringList(sl)
}

// parseFilter makes Filter from comma separated string.
// Eg. check==a,check==b,name==Bob
func parseFilter(filterString string) []*Filter {
	filters := []*Filter{}
	if filterString == "" {
		return filters
	}
	parts := strings.Split(filterString, ",")
	for _, part := range parts {
		keyValue := strings.Split(part, "==")
		if len(keyValue) != 2 {
			continue
		}
		key := keyValue[0]
		value := keyValue[1]
		filters = AppendFilter(filters, key, value)
	}
	return filters
}

// encodeFilter encodes filter to string.
func encodeFilter(filters []*Filter) string {
	var sl []string
	for _, filter := range filters {
		sl = append(sl, filter.QueryString())
	}
	return encodeStringList(sl)
}

func encodeStringList(s []string) string {
	return strings.Join(s, ",")
}
