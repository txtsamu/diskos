# Issue tracker: GitHub

Issues and specs for this repo live in GitHub Issues at `txtsamu/diskos`, the fork. They do **not** live in the upstream parent `b0hemia/diskOS`.

`gh repo view` inside this clone resolves to `b0hemia/diskOS` because the clone has an `upstream` remote. Pass `-R txtsamu/diskos` on every `gh issue`, `gh label`, and writing `gh api` call, and check the target before the first create.

This host has `gh` 2.87.3. Native sub-issues and blocked-by shortcuts need 2.94+, so the `gh api` fallbacks below are the working path.

## Conventions

- **Create an issue**: `gh issue create -R txtsamu/diskos --title "..." --body "..."`. Use a heredoc for multi-line bodies.
- **Read an issue**: `gh issue view <n> -R txtsamu/diskos --json number,title,body,labels,comments`.
- **List issues**: `gh issue list -R txtsamu/diskos --state open --json number,title,body,labels,comments --jq '[.[] | {number, title, body, labels: [.labels[].name], comments: [.comments[].body]}]'` with the appropriate `--label` and `--state` filters.
- **Make an issue a sub-issue of a parent**: `gh api --method POST repos/txtsamu/diskos/issues/<parent>/sub_issues -F sub_issue_id=<child-db-id>`. Get the child database id with `gh api repos/txtsamu/diskos/issues/<n> --jq .id`. Older gh or no sub-issues: put `Part of #<parent>` at the top of the child body.
- **Blocking**: `gh api --method POST repos/txtsamu/diskos/issues/<child>/dependencies/blocked_by -F issue_id=<blocker-db-id>`, where the id is the blocker's numeric database id (`gh api repos/txtsamu/diskos/issues/<n> --jq .id`), not the `#number`. Fallback: `Blocked by: #<n>, #<n>` at the top of the child body.
- **Count open blockers**: `gh api repos/txtsamu/diskos/issues/<n> --jq .issue_dependencies_summary.blocked_by` (0 means startable). `gh issue view <n> -R txtsamu/diskos --json blockedBy` also lists closed blockers, so filter to OPEN.
- **Comment**: `gh issue comment <number> -R txtsamu/diskos --body "..."`.
- **Apply / remove labels**: `gh issue edit <number> -R txtsamu/diskos --add-label "..."` / `--remove-label "..."`.
- **Close**: comment with the explanation, then `gh issue close <number> -R txtsamu/diskos`.

## Pull requests as a triage surface

**PRs as a request surface: no.**

When set to `yes`, PRs run through the same labels and states as issues, using the `gh pr` equivalents, scoped with `-R txtsamu/diskos`.

## When a skill says "publish to the issue tracker"

Create a GitHub issue in `txtsamu/diskos`.

## When a skill says "fetch the relevant ticket"

Read it as in **Read an issue** above.

## Wayfinding operations

Used by `/wayfinder`. The map is a single issue with child issues as tickets.

- **Map**: an issue labelled `wayfinder:map`, holding the Notes / Decisions-so-far / Fog body.
- **Child ticket**: linked as a sub-issue, or added to a task list in the map body with `Part of #<map>` at the top. Labels: `wayfinder:research`, `wayfinder:prototype`, `wayfinder:grilling`, `wayfinder:task`.
- **Blocking**: see **Blocking** above.
- **Frontier query**: the map's open children, drop any with an open blocker or an assignee; first in map order wins.
- **Claim**: `gh issue edit <n> -R txtsamu/diskos --add-assignee @me`.
- **Resolve**: comment the answer, close the issue, then append a context pointer to the map's Decisions-so-far.
