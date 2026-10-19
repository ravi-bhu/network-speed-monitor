# Code signing

Releases are unsigned until the project is accepted by the
[SignPath Foundation](https://signpath.org/) free signing program for open-source projects. Until then every
release ships `SHA256SUMS.txt` next to the installers so you can verify a download:

```
Get-FileHash .\NetworkSpeedMonitor-<version>-x64-setup.exe -Algorithm SHA256
```

Windows SmartScreen may warn about an unsigned installer. That is expected until signing is on.

## Turning signing on

1. Apply to the SignPath Foundation for this repository (https://signpath.org/ , "Open source"). Follow their
   requirements (OSI license, public repository, a code-signing policy page, build from CI).
2. In SignPath, create a project and a signing policy named `release-signing`, and set the project's artifact
   configuration to sign the `.exe` files at the root of the uploaded artifact.
3. In the GitHub repository settings add:
   - secret `SIGNPATH_API_TOKEN`
   - variables `SIGNPATH_ORGANIZATION_ID` and `SIGNPATH_PROJECT_SLUG`
4. In `.github/workflows/release.yml`, change `if: false` to `if: true` on the two steps marked as SignPath
   steps ("Upload unsigned installers for SignPath" and "Sign installers with SignPath"). The signed installers
   overwrite the unsigned ones in `dist\`, and the checksums are written afterwards, so `SHA256SUMS.txt`
   lists the signed files.
5. Tag a release. After the first signed release, update the winget manifests with the new hashes as usual
   (`packaging/winget/README.md`).

Signing the installer does not sign the app executable inside it. To sign that too, add a second SignPath request
before the "Compile installers" step and compile from the signed exe.
