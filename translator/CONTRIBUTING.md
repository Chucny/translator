# Contributing to Chucny's Translator

Issues naming a 32-bit app you want to run are the most useful thing you can send: the app name
and version, where the APK comes from, and what happens when it starts. A runtime report from the
launcher (long-press the game, "Last run report") turns a vague failure into a fixable one.

## Licence and contributions

The project is licensed under the GNU Affero General Public License, version 3.0; see the repository-root `LICENSE`. Third-party components retain their own licenses; see `docs/THIRD_PARTY_NOTICES.md` and the notices within `third_party/`.

By submitting a contribution, you confirm that you have the right to submit it and agree that it may be distributed under the repository's AGPL-3.0 license. Please retain applicable copyright and attribution notices.

## What makes a change easy to accept

- One change per pull request, with the reason in the message: what breaks without it.
- A test that fails before the change and passes after it. `ctest --test-dir build/host` for host
  code, `tools/run_guest_tests.sh` for anything the guest executes.
- No new dependency without saying why the existing ones do not do the job.
- Device-facing changes: say which phone and which app you ran, and what the runtime report said.
