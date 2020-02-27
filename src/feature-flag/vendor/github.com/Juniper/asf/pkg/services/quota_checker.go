package services

// QuotaCheckerCounter implements counting logic for specific resources.
type QuotaCheckerCounter struct {
	SimpleQuotaCounter
}

// QuotaCheckerLimitGetter implements quota limit retrieval for specific resources.
type QuotaCheckerLimitGetter struct {
	SimpleQuotaLimitGetter
}

// NewQuotaCheckerService creates QuotaCheckerService.
func NewQuotaCheckerService(rs ReadService) *QuotaCheckerService {
	return &QuotaCheckerService{
		resourceCounter: &QuotaCheckerCounter{
			SimpleQuotaCounter: SimpleQuotaCounter{rs: rs},
		},
		limitGetter: &QuotaCheckerLimitGetter{
			SimpleQuotaLimitGetter: SimpleQuotaLimitGetter{rs: rs},
		},
	}
}

// TODO (Kamil): implement quota limit retrieval and counting logic for resources
// where the quota information might not be present in the parent:
/*
func (c *QuotaCheckerCounter) CountInstanceIP(...) {
	...
}
*/
// ... and so on.
