# Repository versions

The public repository starts on `main` with one source snapshot. It does not
include the development repository's local branches, tags or release scripts.
The starting development commit is recorded in [SOURCE.md](SOURCE.md).

The existing firmware version remains `0.1.0`. The `v2` and `v3` names in UI
design documents and previews identify layout generations. This publication
does not create matching Git tags.

For future changes, build and run the documented tests before committing
explicit source paths. Keep downloaded dependencies, build directories,
`sdkconfig.local`, generated `sdkconfig`, firmware images and device logs out
of commits. The README explains how to download and recreate the environment.
