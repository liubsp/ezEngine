# Linux profile persistence checks

Run `bash Utilities/Scripts/ProfilePersistence/test.sh` from a Linux checkout with its submodules
initialized. It needs CMake, Ninja, a supported C++ compiler, Python 3, GNU coreutils and the UUID
development headers (`uuid-dev` on Ubuntu). The hosted workflow uses Ubuntu 24.04 and GCC.
No renderer, editor, display or system service is needed.

The script builds Debug FoundationTest and CoreTest under `Workspace/profile-persistence` with
binaries under `Workspace/profile-persistence-output`. It runs profile format/loader checks and a
real-folder small-profile failure test, then the Foundation OS-file and filesystem tests covering
ordinary write, append, seek and read behavior. Each test command has a two-minute outer deadline;
the profile regression's own child has a thirty-second deadline.

The Linux regression temporarily sets only its test child's soft file-size limit to zero and ignores
SIGXFSZ. Opening/truncating the disposable file must succeed before the save is attempted. Limit and
signal state are restored before assertions, cleanup and a successful save/load check. Synthetic
data-directory failures do not substitute for this normal folder/stdio path.

The script also proves that this test distinguishes the missing POSIX drain. It temporarily removes
only the checked `fflush` in `OSFile_Posix.inl`, rebuilds Foundation and requires exactly the expected
save-result assertion failure, not an open failure, crash or timeout. It restores the original source
on exit, rebuilds the corrected Foundation and repeats the positive checks. Use a disposable checkout
or reserve exclusive source/build access while it runs. A failed negative-control run can leave
pre-correction binaries in its dedicated output; rebuild before reusing those outputs.

`results/` below the build directory retains local logs and test JSON, including the expected red
result. These diagnostics and binaries are not release contents. The branch-push/PR workflow runs
the same procedure; a new workflow on a task branch can run through its push trigger without release
packaging or a manual dispatch on the default branch.

The checked POSIX write drain submits libc's buffered bytes to the OS, including for append handles.
It does not call fsync, promise stable-storage durability or change read buffering. Direct small
`ezOSFile` writes no longer batch across calls in libc; use the existing buffered `ezFileWriter` when
batching is needed.
