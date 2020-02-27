package contrailcli

import (
	"fmt"

	"github.com/Juniper/asf/pkg/client"
	"github.com/Juniper/asf/pkg/logutil"
	"github.com/Juniper/asf/pkg/services/baseservices"
	"github.com/spf13/cobra"
)

var (
	filters      string
	pageLimit    int64
	pageMarker   string
	detail       bool
	count        bool
	shared       bool
	excludeHRefs bool
	parentType   string
	parentFQName string
	parentUUIDs  string
	backrefUUIDs string
	objectUUIDs  string
	fields       string
)

func init() {
	ContrailCLI.AddCommand(listCmd)

	// Block of list flags. Please keep synchronized with doc/rest_api.md
	listCmd.Flags().StringVarP(
		&filters,
		baseservices.FiltersKey,
		"f",
		"",
		"Comma-separated filter parameters (e.g. 'check==a,check==b,name==Bob'; default '')",
	)
	listCmd.Flags().Int64VarP(
		&pageLimit,
		baseservices.PageLimitKey,
		"l",
		100,
		"Limit number of returned resources (e.g. '50'; default '100')",
	)
	listCmd.Flags().StringVarP(
		&pageMarker,
		baseservices.PageMarkerKey,
		"m",
		"",
		"Return only the resources with UUIDs lexically greater than the given value "+
			"(e.g. '27e80fa2-a7d3-11e9-803e-abba7e65c022'; default '')",
	)
	listCmd.Flags().BoolVarP(
		&detail,
		baseservices.DetailKey,
		"d",
		false,
		"Detailed response data if 'true' provided (default 'false')",
	)
	listCmd.Flags().BoolVar(
		&count,
		baseservices.CountKey,
		false,
		"Return response with only resource count if 'true' provided (default 'false')",
	)
	listCmd.Flags().BoolVarP(
		&shared,
		baseservices.SharedKey,
		"s",
		false,
		"Include shared object in response if 'true' provided (default 'false')",
	)
	listCmd.Flags().BoolVarP(
		&excludeHRefs,
		baseservices.ExcludeHRefsKey,
		"e",
		false,
		"Exclude hrefs from response if 'true' provided (default 'false') [implementation broken]",
	)
	listCmd.Flags().StringVarP(
		&parentFQName,
		baseservices.ParentFQNameKey,
		"n",
		"",
		"Parent's fully-qualified name as colon-separated list of names "+
			"(e.g. 'default-domain:project-red:vn-red'; default '') [implementation broken]",
	)
	listCmd.Flags().StringVarP(
		&parentType,
		baseservices.ParentTypeKey,
		"t",
		"",
		"Parent's type (e.g. 'project'; default '')",
	)
	listCmd.Flags().StringVarP(
		&parentUUIDs,
		baseservices.ParentUUIDsKey,
		"u",
		"",
		"Comma-separated list of parents' UUIDs "+
			"(e.g. '27e80fa2-a7d3-11e9-803e-abba7e65c022,5195c19a-a7d4-11e9-a7b7-a3e25e96617a'; default '')",
	)
	listCmd.Flags().StringVar(
		&backrefUUIDs,
		baseservices.BackrefUUIDsKey,
		"",
		"Comma-separated list of back references' UUIDs "+
			"(e.g. '27e80fa2-a7d3-11e9-803e-abba7e65c022,5195c19a-a7d4-11e9-a7b7-a3e25e96617a'; default '')",
	)
	// TODO(Daniel): handle RefUUIDs
	listCmd.Flags().StringVar(
		&objectUUIDs,
		baseservices.ObjectUUIDsKey,
		"",
		"Comma-separated list of objects' UUIDs "+
			"(e.g. '27e80fa2-a7d3-11e9-803e-abba7e65c022,5195c19a-a7d4-11e9-a7b7-a3e25e96617a'; default '')",
	)
	listCmd.Flags().StringVar(
		&fields,
		baseservices.FieldsKey,
		"",
		"Comma-separated list of object fields returned in response "+
			"(e.g. 'name,uuid'; default '' does not limit the output)",
	)
}

var listCmd = &cobra.Command{
	Use:   "list [SchemaID]",
	Short: "List data of specified resources",
	Long:  "Invoke command with empty SchemaID in order to show possible usages",
	Run: func(cmd *cobra.Command, args []string) {
		schemaID := ""
		if len(args) > 0 {
			schemaID = args[0]
		}

		cli, err := client.NewCLIByViper()
		if err != nil {
			logutil.FatalWithStackTrace(err)
		}

		r, err := cli.ListResources(
			schemaID,
			&client.ListParameters{
				Filters:      filters,
				PageLimit:    pageLimit,
				PageMarker:   pageMarker,
				Detail:       detail,
				Count:        count,
				Shared:       shared,
				ExcludeHRefs: excludeHRefs,
				ParentFQName: parentFQName,
				ParentType:   parentType,
				ParentUUIDs:  parentUUIDs,
				BackrefUUIDs: backrefUUIDs,
				// TODO(Daniel): handle RefUUIDs
				ObjectUUIDs: objectUUIDs,
				Fields:      fields,
			},
		)
		if err != nil {
			logutil.FatalWithStackTrace(err)
		}

		fmt.Println(r)
	},
}
