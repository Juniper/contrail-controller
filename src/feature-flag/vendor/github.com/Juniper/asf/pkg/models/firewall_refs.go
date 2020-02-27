package models

import (
	"fmt"
	"strings"

	"github.com/Juniper/asf/pkg/models/basemodels"
)

// checkAssociatedRefsInSameScope checks scope of firewall resources references
// so that global firewall resource cannot reference scoped resources.
func checkAssociatedRefsInSameScope(obj basemodels.Object, fqName []string, refs []basemodels.Reference) error {
	if isScopedFirewallResource(fqName) {
		return nil
	}

	for _, ref := range refs {
		if err := checkRefInSameScope(obj, ref, fqName); err != nil {
			return err
		}
	}

	return nil
}

// isScopedFirewallResource checks whether firewall resource is global or scoped.
// Using length of firewall resource one might distinguish a global firewall
// resource (fqName length equal to two) to a scoped one (length longer than two).
// Without this assumtion, checking if references are in the same scope has to be re-worked.
func isScopedFirewallResource(fqName []string) bool {
	return len(fqName) != 2
}

func checkRefInSameScope(obj basemodels.Object, ref basemodels.Reference, fqName []string) error {
	if len(ref.GetTo()) == 2 {
		return nil
	}

	return fmt.Errorf(
		"global %s %s (%s) cannot reference a scoped %s %s (%s)",
		formatResourceKind(obj.Kind()),
		basemodels.FQNameToString(fqName),
		obj.GetUUID(),
		formatResourceKind(ref.GetToKind()),
		basemodels.FQNameToString(ref.GetTo()),
		ref.GetUUID(),
	)
}

func formatResourceKind(kind string) string {
	return strings.Title(strings.Replace(kind, "-", " ", -1))
}
