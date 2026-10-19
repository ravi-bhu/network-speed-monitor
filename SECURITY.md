# Security policy

## Reporting a vulnerability

Please report security problems privately using GitHub's private vulnerability reporting: open the
repository's **Security** tab and choose **Report a vulnerability**. Please don't open a public issue for a
security problem.

Include the version, what you did, and what happened. I'll acknowledge the report and keep you posted on the fix.

## Scope

Network Speed Monitor runs as a standard user, makes no network connections, and stores its data under
`%LOCALAPPDATA%\NetworkSpeedMonitor`. Issues such as reading or corrupting that data, a crash on malformed
files, a way to make the app elevate or connect to the network, or a problem in the installer are all in scope.

Only the latest release is supported.
