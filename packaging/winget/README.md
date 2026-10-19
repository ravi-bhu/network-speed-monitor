# winget packaging

Package identifier: `NetworkSpeedMonitor.NetworkSpeedMonitor`. The installer is the per-user Inno Setup EXE
(`InstallerType: inno`, `Scope: user`), one per architecture (x64 and arm64).

`0.1.0/` holds the three manifest templates (version, installer, default locale), ManifestVersion 1.6.0.
They contain `{{TOKENS}}`, so they are not valid until you fill them in:

| Token | Value |
| --- | --- |
| `{{VERSION}}` | Release version without the `v`, e.g. `0.1.0` |
| `{{RELEASE_DATE}}` | Release date, `YYYY-MM-DD` |
| `{{X64_URL}}`, `{{ARM64_URL}}` | Download URLs of the two installers on the GitHub Release |
| `{{X64_SHA256}}`, `{{ARM64_SHA256}}` | SHA-256 of each installer, from the release's `SHA256SUMS.txt` (uppercase is fine) |
| `{{REPO}}` | `owner/repo` of the GitHub repository |

## Check a manifest

Copy the folder, fill in the tokens, then:

```
winget validate <folder>
winget install --manifest <folder>      # needs: winget settings --enable LocalManifestFiles
```

## Submit a release to microsoft/winget-pkgs

First release: fill in the templates, run `winget validate`, then open a pull request that adds the three files under
`manifests/n/NetworkSpeedMonitor/NetworkSpeedMonitor/<version>/` in your fork of
[microsoft/winget-pkgs](https://github.com/microsoft/winget-pkgs). The file names are the identifier plus
`.installer.yaml` / `.locale.en-US.yaml` (and the bare `.yaml` for the version file), as in the templates.

Later releases are easier with [wingetcreate](https://github.com/microsoft/winget-create), which fills in the
URLs, hashes and version for you and opens the pull request:

```
wingetcreate update NetworkSpeedMonitor.NetworkSpeedMonitor --version <version> --urls <x64-url> <arm64-url> --submit
```

The installer's Inno `AppId` never changes, so winget upgrades replace the existing install in place.
Silent installs and uninstalls (`winget uninstall`) keep the user's usage history; only an interactive uninstall asks.
