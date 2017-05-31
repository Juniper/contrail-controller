# Contrail Community "reboot" of 2017
In order to enable broad participation in the OpenContrail community and allow
contributions of all sorts to the project there are a number of strategic and
tactical topics that need to be addressed. These include the mechanical "hows"
and the more governance oriented "whats" and potentially rules that the
community needs to agree on in order to operate cooperatively in a multivendor
environment.

## Guidelines on How to Contribute to Contrail
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

## Guidelines on When to Contribute to Contrail
* Due dates 
    * An overview of current key dates in the process (blueprint, code
      complete, testing, documentation, beta, GA)
    * Discussion of any changes that might help contributors plan better
    * Dates for dates – set expectations on how and when the key dates for
      future releases will be set
    * Missed date management – discussion of how slipped dates for Juniper’s GA
      release impact community development

## Governance Structure and Questions
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
