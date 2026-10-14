# Releasing Wire

One tag push builds, tests and publishes everything. The workflow is
`.github/workflows/release.yml`.

| What | Where it ends up |
|---|---|
| `wire-windows-setup.exe`: installer, adds wire to PATH | the GitHub release |
| `wire-windows-x86_64.zip` | the GitHub release |
| `wire-linux-x86_64.tar.gz`, `wire-linux-aarch64.tar.gz` (static) | the GitHub release |
| `wire-macos-universal.tar.gz` (Intel + Apple silicon) | the GitHub release |
| `SHA256SUMS` (checked by `install.sh`) | the GitHub release |
| Docker image, amd64 + arm64 | `ghcr.io/<owner>/wire:<version>` and `:latest` |

## First push

Your local repository already has the whole history. Make an **empty**
public repository called `wire` on GitHub (no README, no license, no
.gitignore, so the histories do not clash), then:

```
git remote add origin https://github.com/Husseinabdulameer11/wire.git
git push -u origin main
```

The `ci` workflow runs on that push: Linux (gcc and clang, with Node and
GHDL, so every check runs), Linux with gram installed, macOS, Windows, and
the Docker image.

## One-time setup

1. **Two public repositories under the same account**: `wire` and `gram`.
   The workflows check gram out from `<same owner>/gram`. If gram is named
   differently, change `GRAM_REPO` at the top of both workflow files.
   (A *private* gram would need a personal access token for the checkout.)

2. **After the first release, make the Docker image public.** GitHub creates
   container images as private. Go to your profile → Packages → wire →
   Package settings → Change visibility → Public. You only do this once.

## Making a release

```
git tag v0.1.0
git push origin v0.1.0
```

Then watch the Actions tab. About ten minutes later the release appears
under Releases, with install instructions and the generated changelog.

The version comes from the tag: `v0.1.0` gives `wire --version` → `wire 0.1.0 (gram ...)`.
Tags must look like `v1.2.3` or `v1.2.3-beta.1`.

## Dry run

Actions → release → **Run workflow**. This builds and tests every platform,
including installing and uninstalling the Windows installer on a real
Windows machine. It publishes nothing; the files stay downloadable as
workflow artifacts for a few days. Do this before your first real tag.

## If something fails

- **gram checkout fails**: gram's repository is missing, private, or named
  differently (see `GRAM_REPO`).
- **Docker push denied**: Settings → Actions → General → Workflow
  permissions must allow "Read and write".
- **A release for that tag already exists**: delete the release (and the tag
  if needed) on GitHub, then push the tag again.

gram is built from its `main` branch (`GRAM_REF` in the workflow). Once gram
has version tags of its own, set `GRAM_REF` to one, so every Wire release is
built against a known gram version.
