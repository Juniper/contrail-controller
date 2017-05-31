# Contrail Community "reboot" of 2017
In order to enable broad participation in the OpenContrail community and allow
contributions of all sorts to the project there are a number of strategic and
tactical topics that need to be addressed. These include the mechanical "hows"
and the more governance oriented "whats" and potentially rules that the
community needs to agree on in order to operate cooperatively in a multivendor
environment.

## High Level Prescriptive Steps

* Establish a governance structure
* Establish guidelines for proposing and approving feature scope, design,
  development, CI/CD configuration and release management
* Establish process for revising guidelines based on community governance
* Evaluate current automated test coverage, identify gaps, and establish
  automated test coverage standards for all future development

### Governance Structure
The Linux Foundation provides a good structure to follow. The community should
discuss pros and cons of various structures, but any structure should include
all of the following by one name or another.

* Board of Directors - a group of people responsible for guiding the strategic
  direction of the project
* Technical Steering Committee - a group of people responsible for guiding the
  software design of the project
* Bylaws / governance documents - written documentation which captures the
  agreements of the BoD and TSC which are accessible to and open to comment
  from all contributors

### Guidelines for Proposing and Approving Scope/Design/Code/CI/Release

### Process for Revising Governance Documents

### Test Coverage
An initial assessement of automated test coverage and CI system configuration
is needed. The end state should include all of the following:

* Independent unit tests
* Integration test framework
* Functional testing - full stack
* Performance testing
* Elimination of external dependencies
    * Documentation of any and all external dependencies that can't be
      eliminated
* Integrated DevStack for testing with OpenStack
* Gating on project buildability. Build breaking changes should never be
  merged

## Discussion Topics for OpenContrail Summit and Followup Focus Groups

### Guidelines on How to Contribute to Contrail
In order to contribute to Contrail it is first necessary to be able to build
and deploy locally in order to develop and test new code. It is also
necessary to interact successfully with the Continuous Integration system in
order to diagnose test failures, add new tests and ensure adequate test
coverage. Documentation that needs to be reviewed, revised or created includes:

* Compiling Contrail from source – There has been a fair amount of discussion
  of this on the mailing list recently
    * Discussion of issues people are encountering
    * Build scripts
    * Prerequisites
    * Documentation of the build process
* Testing procedures
    * Discussion of how third party developers should expect to interact with
      Juniper during the time period between code freeze and beta and GA
    * Discussion of beta vs release candidate and whether there are process
      improvements needed in order to ensure that third party developers are
      able to catch and fix bugs in the features they are developing
    * Overview of test framework, not just unit tests but functional and full
      stack automated tests
    * Build system problems – How can non-Juniper contributors effectively
      troubleshoot issues like this:
      https://jenkins.opencontrail.org/job/ci-contrail-vrouter-systest-ubuntu14-mitaka/480/console

Software requires documentation, so contributions of code from the community
must include documentation. The community will develop guidelines on how to
supply documentation.

* Documentation
    * How are non-Juniper developers expected to deliver documentation for
      features that they develop?
    * When is documentation due?

### Guidelines on When to Contribute to Contrail
* Due dates
    * An overview of current key dates in the process (blueprint, code
      complete, testing, documentation, beta, GA)
    * Discussion of any changes that might help contributors plan better
    * Dates for dates – set expectations on how and when the key dates for
      future releases will be set
    * Missed date management – discussion of how slipped dates for Juniper’s GA
      release impact community development

### Governance Structure and Questions
* Core reviewer / TSC / meetings
    * What are the expectations for a non-Juniper developer to become a core
      reviewer?
    * Does Contrail have anything equivalent to a Technical Steering Committee?
    * Are there any regularly scheduled meetings (e.g. on Slack/IRC or
      teleconference) for developers to sync up on progress of their changes,
      discuss code reviews, discuss bugs, etc

* Release Planning
    * How will release dates and corresponding freeze dates be planned?
    * How will the community handle challenges in meeting release dates?
        * Current method: release dates slip at Juniper's discretion
        * OpenStack method: release dates are fixed but unready features
          may be dropped from a release if they don't pass testing.
        * Other method?
    * Blueprint management
        * Which project?
            * https://blueprints.launchpad.net/juniperopenstack
            * https://blueprints.launchpad.net/opencontrail
            * Use both with written guidelines for what goes in each?
        * Expectations for blueprint contents (e.g. OpenStack Neutron typically
          inlcudes links to Gerrit reviews on blueprint whiteboard)
        * Guidelines for updating Definition, Series goal and Milestone target
          fields

### Current Obstacles to Community Contribution
* Ambiguity around what is a release
    * Is there a distinction between a Juniper Contrail release and an Open
      Contrail release?
    * Is there a distinction between code being merged into the Git repo vs
      the corresponding feature being included in a release?
    * Lack of clear procedure about moving blueprints to the Approved state
      and assigning them to milestones
* Uncertainty about review process and target dates
    * How to tell if specs and/or code are on track or at risk
    * What are appropriate steps if a contributor is concerned about whether
      their code will make it into a release
* Continuous Integration and Build System
    * It doesn't appear to be possible for non-Juniper contributors to
      diagnose all CI failures or to adjust CI configuration if necessary
* Documentation
    * We need to establish a CI/Code Review based system for contributing
      documentation so that all contributors can include documentation
      alongside with code
    * We need to establish style and structure conventions so that
      documentation from all contributors looks similar
