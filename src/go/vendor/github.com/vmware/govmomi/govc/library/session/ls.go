/*
Copyright (c) 2019 VMware, Inc. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

	http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

package session

import (
	"context"
	"flag"
	"fmt"
	"io"
	"text/tabwriter"

	"github.com/vmware/govmomi/govc/cli"
	"github.com/vmware/govmomi/govc/flags"
	"github.com/vmware/govmomi/vapi/library"
	"github.com/vmware/govmomi/vapi/rest"
)

type ls struct {
	*flags.ClientFlag
	*flags.OutputFlag
}

func init() {
	cli.Register("library.session.ls", &ls{})
}

func (cmd *ls) Register(ctx context.Context, f *flag.FlagSet) {
	cmd.ClientFlag, ctx = flags.NewClientFlag(ctx)
	cmd.ClientFlag.Register(ctx, f)
	cmd.OutputFlag, ctx = flags.NewOutputFlag(ctx)
	cmd.OutputFlag.Register(ctx, f)
}

func (cmd *ls) Process(ctx context.Context) error {
	if err := cmd.ClientFlag.Process(ctx); err != nil {
		return err
	}
	return cmd.OutputFlag.Process(ctx)
}

func (cmd *ls) Description() string {
	return `List library item update sessions.

Examples:
  govc library.session.ls
  govc library.session.ls -json | jq .`
}

type info struct {
	Sessions []*library.Session
	kind     string
}

func (i *info) Write(w io.Writer) error {
	tw := tabwriter.NewWriter(w, 2, 0, 2, ' ', 0)
	_, _ = fmt.Fprintln(tw, "ID\tItem\tType\tVersion\tProgress\tState\tExpires")

	for _, s := range i.Sessions {
		_, _ = fmt.Fprintf(tw, "%s\t%s\t%s\t%s\t%d\t%s\t%s\n",
			s.ID, s.LibraryItemID, i.kind, s.LibraryItemContentVersion, s.ClientProgress, s.State,
			s.ExpirationTime.Format("2006-01-02 15:04"))
	}

	return tw.Flush()
}

func (cmd *ls) Run(ctx context.Context, f *flag.FlagSet) error {
	return cmd.WithRestClient(ctx, func(c *rest.Client) error {
		m := library.NewManager(c)

		kinds := []struct {
			kind string
			list func(context.Context) ([]string, error)
			get  func(context.Context, string) (*library.Session, error)
		}{
			{"Update", m.ListLibraryItemUpdateSession, m.GetLibraryItemUpdateSession},
			{"Download", m.ListLibraryItemDownloadSession, m.GetLibraryItemDownloadSession},
		}

		for _, k := range kinds {
			ids, err := k.list(ctx)
			if err != nil {
				return err
			}
			if len(ids) == 0 {
				continue
			}
			var sessions []*library.Session

			for _, id := range ids {
				session, err := k.get(ctx, id)
				if err != nil {
					return err
				}
				item, err := m.GetLibraryItem(ctx, session.LibraryItemID)
				if err != nil {
					return err
				}
				lib, err := m.GetLibraryByID(ctx, item.LibraryID)
				if err != nil {
					return err
				}
				session.LibraryItemID = fmt.Sprintf("/%s/%s", lib.Name, item.Name)
				sessions = append(sessions, session)
			}

			err = cmd.WriteResult(&info{sessions, k.kind})
			if err != nil {
				return err
			}
		}
		return nil
	})
}
