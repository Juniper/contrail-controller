package basedb

import (
	"bytes"
	"encoding/json"
	"fmt"
	"net"
	"strconv"
	"strings"

	"github.com/pkg/errors"
	"github.com/sirupsen/logrus"

	"github.com/Juniper/asf/pkg/format"
	"github.com/Juniper/asf/pkg/logutil"
	"github.com/Juniper/asf/pkg/schema"
	"github.com/Juniper/asf/pkg/services/baseservices"
)

//TODO(ijohnson) remove when global share table is supported.
var globalResources = []string{"contrail_cluster", "endpoint"}

// QueryBuilder builds list query.
type QueryBuilder struct {
	Dialect
	Fields        []string
	Table         string
	TableAlias    string
	RefFields     map[string][]string
	ChildFields   map[string][]string
	BackRefFields map[string][]string
}

type authContext interface {
	IsAdmin() bool
	ProjectID() string
	DomainID() string
}

type defaultAuthContext struct{}

// TODO(dfurman): do not assume admin by default
func (defaultAuthContext) IsAdmin() bool     { return true }
func (defaultAuthContext) ProjectID() string { return "project" }
func (defaultAuthContext) DomainID() string  { return "domain" }

type queryContext struct {
	auth        authContext
	values      []interface{}
	columns     Columns
	columnParts []string
	where       []string
	joins       []string
	query       *bytes.Buffer
	spec        *baseservices.ListSpec
}

func newQueryContext(auth authContext, spec *baseservices.ListSpec) *queryContext {
	if auth == nil {
		auth = defaultAuthContext{}
	}
	return &queryContext{
		auth:        auth,
		query:       &bytes.Buffer{},
		columns:     Columns{},
		columnParts: []string{},
		values:      []interface{}{},
		where:       []string{},
		joins:       []string{},
		spec:        spec,
	}
}

// NewQueryBuilder makes a query builder.
func NewQueryBuilder(
	dialect Dialect, table string, fields []string, refFields map[string][]string,
	childFields map[string][]string, backRefFields map[string][]string,
) *QueryBuilder {
	return &QueryBuilder{
		Dialect:       dialect,
		Table:         table,
		TableAlias:    table + "_t",
		Fields:        fields,
		RefFields:     refFields,
		ChildFields:   childFields,
		BackRefFields: backRefFields,
	}
}

// NewDialect creates NewDialect objects.
func NewDialect() Dialect {
	return Dialect{
		QuoteRune:          `"`,
		JSONAggFuncStart:   "json_agg(json_build_object(",
		JSONAggFuncEnd:     "))",
		AnyValueString:     "",
		PlaceHolderIndex:   true,
		IPLiteralPrefix:    "inet '",
		PpLiteralSuffix:    "'",
		SelectIPPrefix:     `"`,
		SelectIPSuffix:     `"`,
		ConstraintsDisable: "SET session_replication_role = replica;",
		ConstraintsEnable:  "SET session_replication_role = DEFAULT;",
		CastColumnToText:   "%s :: TEXT",
	}
}

// Dialect represents database dialect.
type Dialect struct {
	QuoteRune          string
	JSONAggFuncStart   string
	JSONAggFuncEnd     string
	AnyValueString     string
	PlaceHolderIndex   bool
	IPLiteralPrefix    string
	PpLiteralSuffix    string
	SelectIPPrefix     string
	SelectIPSuffix     string
	ConstraintsDisable string
	ConstraintsEnable  string
	CastColumnToText   string
}

// DisableConstraints gives statement for disabling constraint checking (use with caution!)
func (d *Dialect) DisableConstraints() string {
	return d.ConstraintsDisable
}

// EnableConstraints gives statement that enables constraint checking - reverse behavior of DisableConstraints
func (d *Dialect) EnableConstraints() string {
	return d.ConstraintsEnable
}

// Quote quote with DB specific way.
func (d *Dialect) Quote(params ...string) string {
	query := ""
	l := len(params)
	for i := 0; i < l-1; i++ {
		query += d.QuoteRune + strings.ToLower(params[i]) + d.QuoteRune + "."
	}
	query += d.QuoteRune + strings.ToLower(params[l-1]) + d.QuoteRune
	return query
}

// CastToText makes column treated as text field.
func (d *Dialect) CastToText(column string) string {
	return fmt.Sprintf(d.CastColumnToText, column)
}

// Placeholder returns DB specific placeholder.
func (d *Dialect) Placeholder(i int) string {
	if d.PlaceHolderIndex {
		return "$" + strconv.Itoa(i)
	}
	return "?"
}

// Values makes list of place holders.
func (d *Dialect) Values(params ...string) string {
	query, _ := d.ValuesWithIndex(0, params...)
	return query
}

// ValuesWithIndex makes list of place holders from provided index
func (d *Dialect) ValuesWithIndex(index int, params ...string) (string, int) {
	query := ""
	lastIndex := index + len(params)
	for ; index < lastIndex-1; index++ {
		query += d.Placeholder(index+1) + ","
	}
	query += d.Placeholder(lastIndex)
	return query, lastIndex
}

// QuoteSep quotes with separator.
func (d *Dialect) QuoteSep(params ...string) string {
	query := ""
	l := len(params)
	for i := 0; i < l-1; i++ {
		query += d.QuoteRune + strings.ToLower(params[i]) + d.QuoteRune + ","
	}
	query += d.QuoteRune + strings.ToLower(params[l-1]) + d.QuoteRune
	return query
}

func (d *Dialect) anyValue(params ...string) string {
	if d.AnyValueString != "" {
		return d.AnyValueString + d.Quote(params...) + ")"
	}
	return d.Quote(params...)
}

//LiteralIP returns ipv6 ip with db specific way.
func (d *Dialect) LiteralIP(ip net.IP) string {
	return d.IPLiteralPrefix + StringIPv6(ip) + d.PpLiteralSuffix
}

//SelectIP selects ip with db specific way.
func (d *Dialect) SelectIP(columnName string) string {
	return d.SelectIPPrefix + columnName + d.SelectIPSuffix
}

//Columns represents column index.
type Columns map[string]int

func (qb *QueryBuilder) buildFilterParts(ctx *queryContext, column string, filterValues []string) string {
	var filterQuery bytes.Buffer
	if len(filterValues) == 1 {
		ctx.values = append(ctx.values, filterValues[0])
		WriteStrings(&filterQuery, qb.CastToText(column), " = ", qb.Placeholder(len(ctx.values)))
	} else {
		WriteStrings(&filterQuery, qb.CastToText(column), " in (")
		last := len(filterValues) - 1
		for _, value := range filterValues[:last] {
			ctx.values = append(ctx.values, value)
			WriteStrings(&filterQuery, qb.Placeholder(len(ctx.values)), ",")
		}
		ctx.values = append(ctx.values, filterValues[last])
		WriteStrings(&filterQuery, qb.Placeholder(len(ctx.values)), ")")
	}
	return filterQuery.String()
}

func (qb *QueryBuilder) buildParentFQNameFilter(ctx *queryContext, columnJSON string, filterValues []string) []string {
	lines := make([]string, 0, len(filterValues)+1)

	for ind, value := range filterValues {
		ctx.values = append(ctx.values, value)
		lines = append(lines, fmt.Sprintf("%s ->> %d = %s", columnJSON, ind, qb.Placeholder(len(ctx.values))))
	}
	lines = append(lines, fmt.Sprintf("json_array_length(%s) = %d", columnJSON, len(filterValues)+1))

	return lines
}

func (qb *QueryBuilder) join(fromTable, fromProperty, toTable string) string {
	return "left join " + qb.Quote(fromTable) + " on " +
		qb.Quote(toTable, "uuid") + " = " + qb.Quote(fromTable, fromProperty)
}

func (qb *QueryBuilder) as(a, b string) string {
	return a + " as " + b
}

func (qb *QueryBuilder) buildFilterQuery(ctx *queryContext) {
	spec := ctx.spec
	filters := spec.Filters
	filters = baseservices.AppendFilter(filters, "uuid", spec.ObjectUUIDs...)
	filters = baseservices.AppendFilter(filters, "parent_uuid", spec.ParentUUIDs...)
	if spec.ParentType != "" {
		filters = baseservices.AppendFilter(filters, "parent_type", spec.ParentType)
	}
	if spec.ParentFQName != nil {
		column := qb.Quote(qb.TableAlias, "fq_name")
		wheres := qb.buildParentFQNameFilter(ctx, column, spec.ParentFQName)
		ctx.where = append(ctx.where, wheres...)
	}
	for _, filter := range filters {
		if !qb.isValidField(filter.Key) {
			continue
		}
		column := qb.Quote(qb.TableAlias, filter.Key)
		where := qb.buildFilterParts(ctx, column, filter.Values)
		ctx.where = append(ctx.where, where)
	}

	qb.buildBackrefFilterQuery(ctx)

	qb.buildRefFilterQuery(ctx)

	// Add condition to start query items from next to markered item's uuid
	if spec.Marker != "" {
		ctx.values = append(ctx.values, spec.Marker)
		ctx.where = append(ctx.where,
			fmt.Sprintf("%s > %s", qb.Quote(qb.TableAlias, "uuid"), qb.Placeholder(len(ctx.values))))
	}
}

func (qb *QueryBuilder) buildBackrefFilterQuery(ctx *queryContext) {
	spec := ctx.spec
	if len(spec.BackRefUUIDs) == 0 {
		return
	}
	where := []string{}
	for backrefTable := range qb.BackRefFields {
		refTable := schema.ReferenceTableName(schema.RefPrefix, backrefTable, qb.Table)
		ctx.joins = append(ctx.joins, qb.join(refTable, "to", qb.TableAlias))
		wherePart := qb.buildFilterParts(ctx, qb.Quote(refTable, "from"), spec.BackRefUUIDs)
		where = append(where, wherePart)
	}
	ctx.where = append(ctx.where, "("+strings.Join(where, " or ")+")")
}

func (qb *QueryBuilder) buildRefFilterQuery(ctx *queryContext) {
	spec := ctx.spec
	if len(spec.RefUUIDs) == 0 {
		return
	}
	where := []string{}
	for refField := range qb.RefFields {
		rus, ok := spec.RefUUIDs[refField+"_refs"]
		if !ok || len(rus.UUIDs) == 0 {
			continue
		}
		refTable := schema.ReferenceTableName(schema.RefPrefix, qb.Table, refField)
		ctx.joins = append(ctx.joins, qb.join(refTable, "from", qb.TableAlias))
		wherePart := qb.buildFilterParts(ctx, qb.Quote(refTable, "to"), rus.UUIDs)
		where = append(where, wherePart)
	}
	if len(where) > 0 {
		ctx.where = append(ctx.where, "("+strings.Join(where, " and ")+")")
	}
}

func (qb *QueryBuilder) buildAuthQuery(ctx *queryContext) {
	auth := ctx.auth
	spec := ctx.spec
	where := []string{}

	//TODO(ijohnson) support global share table
	if !auth.IsAdmin() && !format.ContainsString(globalResources, qb.Table) {
		ctx.values = append(ctx.values, auth.ProjectID())
		where = append(where, qb.Quote(qb.TableAlias, "owner")+" = "+qb.Placeholder(len(ctx.values)))
		if spec.Shared {
			shareTables := []string{"domain_share_" + qb.Table, "tenant_share_" + qb.Table}
			for i, shareTable := range shareTables {
				ctx.joins = append(ctx.joins,
					qb.join(shareTable, "uuid", qb.TableAlias))
				where = append(where, fmt.Sprintf("(%s.to = %s and %s.access >= 4)",
					qb.Quote(shareTable), qb.Placeholder(len(ctx.values)+i+1), qb.Quote(shareTable)))
			}
			ctx.values = append(ctx.values, auth.DomainID(), auth.ProjectID())
		}
	}
	if len(where) > 0 {
		ctx.where = append(ctx.where, fmt.Sprintf("(%s)", strings.Join(where, " or ")))
	}
}

func (qb *QueryBuilder) buildQuery(ctx *queryContext) {
	spec := ctx.spec
	query := ctx.query
	writeString(query, "select ")

	if len(ctx.columnParts) != len(ctx.columns) {
		logutil.FatalWithStackTrace(errors.Errorf(
			"QueryBuilder: columnParts and columns do not have equal length: qb: %+v, ctx: %+v", qb, ctx),
		)
	}
	WriteStrings(query, strings.Join(ctx.columnParts, ","), " from ", qb.as(qb.Table, qb.TableAlias))

	if len(ctx.joins) > 0 {
		WriteStrings(query, " ", strings.Join(ctx.joins, " "))
	}
	if len(ctx.where) > 0 {
		WriteStrings(query, " where ", strings.Join(ctx.where, " and "))
	}

	// We use 'group by' to eliminate duplicates arising from using joins.
	// TODO (Kamil): we should consider a perhaps more efficient "WHERE EXISTS" query instead of using joins.
	if spec.Shared || len(spec.BackRefUUIDs) > 0 {
		WriteStrings(query, " group by ", qb.Quote(qb.TableAlias, "uuid"))
	}

	// Ordering by uuid to support pagination with uuid index.
	WriteStrings(query, " order by ", qb.Quote(qb.TableAlias, "uuid"))

	if spec.Limit > 0 {
		WriteStrings(
			query,
			" limit ",
			strconv.FormatInt(spec.Limit, 10),
		)
	}
}

func (qb *QueryBuilder) islinkToInField(ctx *queryContext, linkTo string) bool {
	spec := ctx.spec
	if len(spec.Fields) == 0 {
		return true
	}
	for _, field := range spec.Fields {
		if field == linkTo {
			return true
		}
	}
	return false
}

func (qb *QueryBuilder) buildRefQuery(ctx *queryContext) {
	spec := ctx.spec
	if !spec.Detail {
		return
	}
	for linkTo, refFields := range qb.RefFields {
		if !qb.islinkToInField(ctx, linkTo+"_refs") {
			continue
		}
		refTable := schema.ReferenceTableName(schema.RefPrefix, qb.Table, linkTo)
		refFields = append(refFields, "from")
		refFields = append(refFields, "to")
		subQuery := "(select " +
			qb.as(qb.jsonAggRef(refTable+"_t", refFields...), qb.Quote(refTable+"_ref")) +
			" from " + qb.as(qb.Quote(refTable), refTable+"_t") +
			" left join " + "metadata" +
			" on " + qb.Quote(refTable+"_t", "to") + " = " + qb.Quote("metadata", "uuid") +
			" where " + qb.Quote(qb.TableAlias, "uuid") + " = " + qb.Quote(refTable+"_t", "from") +
			" group by " + qb.Quote(refTable+"_t", "from") + " )"
		ctx.columnParts = append(
			ctx.columnParts,
			subQuery)
		ctx.columns["ref_"+linkTo] = len(ctx.columns)
	}
}

func (d *Dialect) jsonAggBase(table string, params ...string) string {
	return "row_to_json(" + d.Quote(table) + ")"
}

func (d *Dialect) jsonAgg(table string, params ...string) string {
	return "json_agg(" + d.jsonAggBase(table, params...) + ")"
}

func (d *Dialect) jsonAggRef(table string, params ...string) string {
	fqNameJSON := "json_build_object('fq_name', metadata.fq_name)"
	return "json_agg((" + d.jsonAggBase(table, params...) + ")::jsonb || " + fqNameJSON + "::jsonb)"
}

func (qb *QueryBuilder) buildChildQuery(ctx *queryContext) {
	spec := ctx.spec
	if !spec.Detail {
		return
	}
	for child, childFields := range qb.ChildFields {
		if !qb.islinkToInField(ctx, child+"s") {
			continue
		}
		child = strings.ToLower(child)
		subQuery := "(select " +
			qb.as(qb.jsonAgg(child+"_t", childFields...), qb.Quote(child+"_ref")) +
			" from " + qb.as(qb.Quote(child), child+"_t") +
			" where " + qb.Quote(qb.TableAlias, "uuid") + " = " + qb.Quote(child+"_t", "parent_uuid") +
			" group by " + qb.Quote(child+"_t", "parent_uuid") + " )"
		ctx.columnParts = append(
			ctx.columnParts,
			subQuery)
		ctx.columns[schema.ChildColumnName(child, qb.Table)] = len(ctx.columns)
	}
}

func (qb *QueryBuilder) buildBackRefQuery(ctx *queryContext) {
	spec := ctx.spec
	if !spec.Detail {
		return
	}
	for backrefTable, backrefFields := range qb.BackRefFields {
		if !qb.islinkToInField(ctx, backrefTable+"back_refs") {
			continue
		}
		refTable := schema.ReferenceTableName(schema.RefPrefix, backrefTable, qb.Table)
		backrefTable = strings.ToLower(backrefTable)
		subQuery := "(select " +
			qb.as(qb.jsonAgg(backrefTable+"_t", backrefFields...), qb.Quote(refTable+"_backref")) +
			" from " + qb.as(qb.Quote(backrefTable), backrefTable+"_t") +
			" inner join " + qb.as(refTable, refTable+"_t") +
			" on " + qb.Quote(refTable+"_t", "from") + " = " + qb.Quote(backrefTable+"_t", "uuid") +
			" where " + qb.Quote(refTable+"_t", "to") + " = " + qb.Quote(qb.TableAlias, "uuid") + " )"
		ctx.columnParts = append(
			ctx.columnParts,
			subQuery)
		ctx.columns[schema.BackRefColumnName(backrefTable, qb.Table)] = len(ctx.columns)
	}
}

func (qb *QueryBuilder) isValidField(requestedField string) bool {
	for _, field := range qb.Fields {
		if field == requestedField {
			return true
		}
	}
	return false
}

func (qb *QueryBuilder) sanitizeFields(ctx *queryContext) []string {
	if ctx.spec.Fields == nil {
		return nil
	}
	newlist := make([]string, 0, len(ctx.spec.Fields))
	for _, f := range ctx.spec.Fields {
		if qb.isValidField(f) {
			newlist = append(newlist, f)
		}
	}
	return newlist
}

func (qb *QueryBuilder) buildColumns(ctx *queryContext) {
	spec := ctx.spec
	fields := qb.Fields
	if spec.Fields != nil && spec.Detail == false {
		fields = qb.sanitizeFields(ctx)
	}

	if spec.Shared || len(spec.BackRefUUIDs) > 0 {
		for _, column := range fields {
			ctx.columns[column] = len(ctx.columns)
			ctx.columnParts = append(ctx.columnParts, qb.anyValue(qb.TableAlias, column))
		}
	} else {
		for _, column := range fields {
			ctx.columns[column] = len(ctx.columns)
			ctx.columnParts = append(ctx.columnParts, qb.Quote(qb.TableAlias, column))
		}
	}
}

//ListQuery makes sql query.
func (qb *QueryBuilder) ListQuery(auth authContext, spec *baseservices.ListSpec) (string, Columns, []interface{}) {
	ctx := newQueryContext(auth, spec)
	qb.buildColumns(ctx)
	qb.buildFilterQuery(ctx)
	qb.buildAuthQuery(ctx)
	qb.buildRefQuery(ctx)
	qb.buildChildQuery(ctx)
	qb.buildBackRefQuery(ctx)
	qb.buildQuery(ctx)
	return ctx.query.String(), ctx.columns, ctx.values
}

// CountQuery makes an SQL query which counts the number of specified resources.
func (qb *QueryBuilder) CountQuery(auth authContext, spec *baseservices.ListSpec) (string, []interface{}) {
	ctx := newQueryContext(auth, spec)

	qb.buildFilterQuery(ctx)
	qb.buildAuthQuery(ctx)

	query := ctx.query

	// We use 'distinct' to eliminate duplicates arising from using joins.
	// TODO (Kamil): consider using 'COUNT(*)' when no joins are used.
	// More tests should be performed to check if it causes any significant performance changes.
	WriteStrings(query, "select count(distinct ", qb.Quote(qb.TableAlias, "uuid"), ") from ",
		qb.as(qb.Table, qb.TableAlias), " ")

	if len(ctx.joins) > 0 {
		writeString(query, strings.Join(ctx.joins, " "))
	}
	if len(ctx.where) > 0 {
		WriteStrings(query, " where ", strings.Join(ctx.where, " and "))
	}

	// TODO (Kamil): should we handle Limit in count?

	return ctx.query.String(), ctx.values
}

//CreateQuery makes sql query.
func (qb *QueryBuilder) CreateQuery() string {
	query := ("insert into " + qb.Quote(qb.Table) + "(" +
		qb.QuoteSep(qb.Fields...) + ") Values (" + qb.Values(qb.Fields...) + ")")
	return query
}

//CreateRefQuery makes a reference.
func (qb *QueryBuilder) CreateRefQuery(linkTo string) string {
	fields := append([]string{"from", "to"}, qb.RefFields[linkTo]...)
	table := schema.ReferenceTableName(schema.RefPrefix, qb.Table, linkTo)
	return ("insert into " + table +
		" (" + qb.QuoteSep(fields...) + ") Values (" + qb.Values(fields...) + ")")
}

//CreateParentRefQuery makes a reference to parent object.
func (qb *QueryBuilder) CreateParentRefQuery(linkTo string) string {
	fields := []string{"from", "to"}
	table := schema.ReferenceTableName(schema.ParentPrefix, qb.Table, linkTo)
	return ("insert into " + table +
		" (" + qb.QuoteSep(fields...) + ") Values (" + qb.Values(fields...) + ")")
}

//DeleteQuery makes sql query.
func (qb *QueryBuilder) DeleteQuery() string {
	return "delete from " + qb.Quote(qb.Table) + " where uuid = " + qb.Placeholder(1)
}

// DeleteRefsQuery makes sql query deleting refs to specified type from single object.
func (qb *QueryBuilder) DeleteRefsQuery(linkTo string) string {
	table := schema.ReferenceTableName(schema.RefPrefix, qb.Table, linkTo)
	return "delete from " + table + " where " + qb.Quote("from") + " = " + qb.Placeholder(1)
}

// DeleteRelaxedBackrefsQuery makes sql query that deletes all backrefs marked as relaxed.
func (qb *QueryBuilder) DeleteRelaxedBackrefsQuery(linkFrom string) string {
	table := schema.ReferenceTableName(schema.RefPrefix, linkFrom, qb.Table)
	return fmt.Sprintf("delete from %s where %s = %s and %s = true",
		table, qb.Quote("to"), qb.Placeholder(1), qb.Quote("relaxed"))
}

// DeleteRefQuery makes sql query deleting single ref entry.
func (qb *QueryBuilder) DeleteRefQuery(linkTo string) string {
	table := schema.ReferenceTableName(schema.RefPrefix, qb.Table, linkTo)
	return fmt.Sprintf("delete from %s where %s = %s and %s = %s",
		table, qb.Quote("from"), qb.Placeholder(1), qb.Quote("to"), qb.Placeholder(2))
}

// RelaxRefQuery makes sql query that marks a reference as relaxed.
func (qb *QueryBuilder) RelaxRefQuery(linkTo string) string {
	table := schema.ReferenceTableName(schema.RefPrefix, qb.Table, linkTo)
	return fmt.Sprintf("update %s set %s = true where %s = %s and %s = %s",
		table, qb.Quote("relaxed"),
		qb.Quote("from"), qb.Placeholder(1), qb.Quote("to"), qb.Placeholder(2))
}

//SelectAuthQuery makes sql query.
func (qb *QueryBuilder) SelectAuthQuery(admin bool) string {
	query := "select count(uuid) from " + qb.Quote(qb.Table) + " where uuid = " + qb.Placeholder(1)
	if !admin {
		query += " and owner = " + qb.Placeholder(2)
	}
	return query
}

//UpdateQuery makes sql query for update.
func (qb *QueryBuilder) UpdateQuery(columns []string) string {
	var query bytes.Buffer
	WriteStrings(&query, "update ", qb.Quote(qb.Table))
	if len(columns) > 0 {
		query.WriteString(" set ")
		for i, column := range columns {
			WriteStrings(&query, qb.Quote(column), " = ", qb.Placeholder(i+1))
			if i < len(columns)-1 {
				writeString(&query, ", ")
			}
		}
	}
	WriteStrings(&query, " where uuid = ", qb.Placeholder(len(columns)+1))
	return query.String()
}

//ScanResourceList scan list.
func (qb *QueryBuilder) ScanResourceList(value interface{}) []interface{} {
	var resources []interface{}
	var stringValue, err = format.InterfaceToStringE(value)
	if stringValue == "" || err != nil {
		return nil
	}

	if err := json.Unmarshal([]byte(stringValue), &resources); err != nil {
		logrus.WithError(err).WithField("value", value).Debug("query builder: failed to scan resource list - ignoring")
		return nil
	}

	return resources
}

// StringIPv6 serializes ip address, forces ipv6 format.
func StringIPv6(ip net.IP) string {
	if ip == nil || len(ip) == 0 {
		return ""
	}
	if ip.To4() == nil {
		return ip.String()
	}

	res := make(net.IP, len(ip))
	copy(res, ip)
	res = res.To16()
	res[1] = 1
	return res.String()[1:]
}

// WriteStrings writes multiple strings to given buffer.
func WriteStrings(b *bytes.Buffer, strings ...string) {
	for _, s := range strings {
		writeString(b, s)
	}
}

// writeString calls bytes.Buffer.WriteString() and strips its signature from redundant error,
// which  in current implementation is always nil.
// See: https://golang.org/pkg/bytes/#Buffer.WriteString
func writeString(b *bytes.Buffer, s string) {
	_, err := b.WriteString(s)
	if err != nil {
		logutil.FatalWithStackTrace(errors.Wrapf(err, "bytes.Buffer.WriteString() failed"))
	}
}
