# Code signing

Releases are unsigned until the project is accepted by the
[SignPath Foundation](https://signpath.org/) free signing program for open-source projects. Until then every
release ships `SHA256SUMS.txt` next to the installers so you can verify a download:

```
Get-FileHash .\NetworkSpeedMonitor-<version>-x64-setup.exe -Algorithm SHA256
```

Windows SmartScreen may warn about an unsigned installer. That is expected until signing is on.

The policy itself (who approves signing, the privacy statement) is the "Code signing policy" section of the
[README](../README.md#code-signing-policy).

## What SignPath requires (already in place)

- An OSI license (MIT), a public repository, and builds from CI (`.github/workflows/release.yml`).
- A released project: SignPath only accepts projects that already have a release in the form to be signed.
- A "Code signing policy" section on the home page (README) and the term on the release page (the release
  workflow adds a "Code signing policy" link to every release's notes).
- Product name and version metadata on every signed file: the app's VERSIONINFO (`res/app.rc`) and the
  installer's `VersionInfo*` settings both say "Network Speed Monitor" and the release version.
- Multi-factor authentication for every team member, on GitHub and on SignPath.

## Turning signing on

1. Apply to the SignPath Foundation for this repository (https://signpath.org/apply). _Applied 2026-10-19,
   after v0.1.0; waiting for their reply._
2. In SignPath, create a project with a signing policy named `release-signing` and two artifact configurations:
   - `app`: a zip with `x64/NetworkSpeedMonitor.exe` and `arm64/NetworkSpeedMonitor.exe`; sign both exes.
   - `installers`: a zip with the `.exe` files at its root; sign them.

   Restrict product name to `Network Speed Monitor` in both, and require manual approval for the policy.
3. In the GitHub repository settings add:
   - secret `SIGNPATH_API_TOKEN`
   - variables `SIGNPATH_ORGANIZATION_ID` and `SIGNPATH_PROJECT_SLUG`
4. In `.github/workflows/release.yml`, change `if: false` to `if: true` on the four SignPath steps:
   "Upload unsigned app for SignPath" and "Sign the app with SignPath" (the signed exes replace the staged ones
   before the installers are compiled), then "Upload unsigned installers for SignPath" and "Sign installers
   with SignPath" (the signed installers replace the unsigned ones in `dist\`). The checksums are written
   afterwards, so `SHA256SUMS.txt` lists the signed files.
5. Tag a release and approve both signing requests in SignPath when the workflow waits for them. After the
   first signed release, update the winget manifests with the new hashes as usual
   (`packaging/winget/README.md`).
