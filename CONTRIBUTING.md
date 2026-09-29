# Contributing

Bug reports, fixes, and documentation improvements are welcome.

## Reporting a problem

Check the [installation guide](README.md#install) and search existing issues first. In your report, include:

- The release you installed and your Vita model.
- What you expected, what happened, and how to reproduce it.
- The mission or location, if it happens during gameplay.
- `ux0:data/reLCS/log.txt`, or `intro.log` in the same folder for intro problems.

Copy the logs before launching again; they are replaced on startup. If Vita created a `.psp2dmp`, attach it with the report. Please do not upload game data or system modules.

## Sending a change

Start with the [build guide](vita/README.md). Keep each pull request focused and explain what changed and how you checked it. For performance changes, include a before/after comparison on the same route or mission.

The [test guide](docs/DEVELOPMENT.md) lists the host checks. Note which checks you ran and whether you tested on a Vita. Leave generated files and game data out of commits.
