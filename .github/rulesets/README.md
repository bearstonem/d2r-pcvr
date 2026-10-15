# Main branch and release protections

These JSON files record the intended GitHub rulesets for
`bearstonem/d2r-pcvr`. Committing the files does **not** apply them: GitHub stores
and enforces live rules separately in
[Settings > Rules > Rulesets](https://github.com/bearstonem/d2r-pcvr/settings/rules).
They can be imported there or applied through the GitHub REST API.

## Main integrity

`main-integrity.json` requires a pull request, resolves review conversations
before merge, blocks force pushes and prevents deletion of `main`.
**There are no bypass actors, including the owner.** Normal updates must go
through a PR, even for maintainers.

## Maintainer review

`maintainer-review.json` requires one approving review, requires code-owner
review, and dismisses approvals after new reviewable commits. The code owner
for all files is `@bearstonem`, as listed in [CODEOWNERS](../CODEOWNERS).

GitHub does not let authors approve their own PRs. To keep owner-authored
maintenance possible, this second ruleset gives `@bearstonem` (GitHub user ID
`75600543`) a **pull-request-only** review bypass. The owner should review and
approve outside contributions normally; the exception is intended for their
own PRs. It does not override the separate main-integrity ruleset, permit
direct pushes or allow unresolved review conversations to be merged.

See GitHub's explanation of
[pull-request-only bypass](https://docs.github.com/en/repositories/configuring-branches-and-merges-in-your-repository/managing-rulesets/creating-rulesets-for-a-repository#granting-bypass-permissions-for-your-ruleset).

## Release tags

`release-tags.json` restricts creating, updating and deleting tags matching
`v*` to `@bearstonem` (GitHub user ID `75600543`). Its only bypass actor is that
user, with `always` scope because tags do not use pull requests. This exception
applies only to the tag ruleset; it grants no exception to either main-branch
ruleset. Collaborators can continue working on topic branches and submitting PRs.

This protects existing and future matching tags. It does not remove the
release-management permissions GitHub includes with collaborator write access.
Other tag names are outside this ruleset.

## Immutable releases

Keep **Settings > General > Releases > Enable release immutability** enabled.
This is a separate repository setting, not part of the ruleset JSON files.
It can also be enabled with `PUT /repos/bearstonem/d2r-pcvr/immutable-releases`
and checked with `GET` at the same endpoint.

Publish releases as drafts first: attach every reviewed asset and checksum,
verify them, then publish. Once an immutable release is published, its assets
cannot be added, replaced or individually deleted, and its tag cannot be moved
or deleted while the release exists. Corrections to binaries require a new
release version. Release titles, notes and latest/prerelease status can still
be edited; release deletion remains possible. See
[GitHub's immutable release documentation](https://docs.github.com/en/code-security/concepts/supply-chain-security/immutable-releases).

Enabling this setting applies to future releases only. Existing releases,
including `v0.3.15-windows-preview`, do not become immutable automatically.
Their matching tags are covered by the release-tag ruleset, but their attached
assets retain their previous permissions.

These controls do not make collaborator write access PR-only. Use fork-based
contributions when a contributor should only propose changes without direct
repository or release-management access.

## Maintenance

- Keep all three rulesets active and release immutability enabled. Do not add
  a bypass to main integrity to land a change.
- Outside contributors fork and open a PR; maintainers use topic branches and PRs.
  This contribution policy is documented in [CONTRIBUTING.md](../../CONTRIBUTING.md).
  GitHub repository access controls prevent outside contributors from creating
  branches or pushing in this repository; branch rules require PRs into `main`.
- Update these files through a PR when intentionally changing the policy, then
  apply and verify the matching live GitHub settings. Do not create duplicate
  rulesets when updating an existing rule.
- If maintainership changes, update CODEOWNERS, the owner review exception and
  the release-tag owner exception together.
- No build or test status checks are required by these initial rulesets. Reviewers
  must assess the validation reported in each PR. Add required checks only once
  the corresponding automation is running reliably.
