# Repository collaboration

Read [CONTRIBUTING.md](CONTRIBUTING.md) before changing this repository.
Then read [the agent workflow guide](docs/AGENT_WORKFLOW.md) for the established
build, install, launch and debugging procedures. If present, also read the local
`build-frame/AGENT_LOCAL_STATE.md` handoff before rediscovering machine paths or
resuming an investigation. Keep these notes current when a workflow is verified.

- Work on a topic branch and submit a pull request against `main`. Outside
  contributors must use a fork; maintainers may use repository branches.
- Do not commit or push directly to `main`, force push it, delete it, or disable
  its pull request protections to deliver a change.
- Keep changes focused and run checks appropriate to the affected behavior.
  Report what was actually verified, including any missing headset testing.
- Preserve upstream credits, licensing, player saves and unmodded desktop play.
- Do not add game assets, extracted caches, memory dumps, saves, downloaded
  dependencies, build output or secrets to commits or release attachments.
- Game testing uses offline local characters only.
