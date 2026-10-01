# Contributing to Lindar

Lindar is in preview. This is the time to discuss even substantial changes to the API, naming conventions, module boundaries and architecture. Proposals and discussion are welcome before these choices settle for 1.0.

Thank you for your interest in Lindar. Code, bug reports, API feedback and documentation improvements are all welcome.

## Issues and proposals

Please report bugs, unclear or inconsistent API behaviour, and gaps in the documentation through GitHub Issues. For bugs, include the platform, build configuration and a small reproducer where possible. For API feedback, describe what you were trying to do and what was unclear or missing. See the [security policy](SECURITY.md) for when to report a problem privately.

Discuss larger changes in an Issue before starting a pull request, particularly changes to the public API, architecture or dependencies. Agreeing on the scope first helps avoid spending time on work that needs a different approach.

## Code and modules

Keep changes consistent with the current API and its conventions for naming, ownership, error handling and module boundaries. If a convention needs to change, explain the proposed change in the Issue.

New modules are welcome. Follow the [module architecture](architecture.md), declare dependencies explicitly and keep platform-specific requirements within the relevant modules.

Prefer clear, concise code. Use comments for non-obvious decisions and document public declarations in the headers. Include relevant tests for changed behaviour and describe how you checked the change; see [test runners and tooling](testing.md).

## Third-party dependencies

Additional third-party libraries under the LGPL or more permissive licences are welcome. Explain why the dependency is needed and identify its upstream project and licence. Pin it as a Git submodule to a stable release, or an explicit commit if no release exists, and update [the dependency record](../vendor/VERSIONS.md). Preserve upstream licence and attribution files.

## Contributor Licence Agreement

Open a pull request and follow the CLA bot's prompt. Read the linked [Contributor Licence Agreement](cla.md), then post the acceptance declaration supplied by the bot from your GitHub account. The bot records acceptance and updates the `CLA` check.

Acceptance is remembered for that exact agreement text. Each later Contribution submitted under the accepted revision is licensed on those terms when submitted. Contributors retain copyright; Dawid Pieper receives relicensing rights, including commercial and proprietary licensing.

If an organisation owns your work, identify it in the pull request and confirm your authority to contribute on its behalf. Disclose third-party material and its licence. Each contributor must satisfy the CLA check.
