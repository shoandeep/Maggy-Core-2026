# Security

## Current posture (interim hardening)

The Realtime Database previously allowed **public read/write** — anyone with the
database URL could control any robot and read its telemetry. That is what
Firebase's "insecure rules" notification was warning about.

It is now locked behind authentication:

- **RTDB rules** (`database.rules.json`) require `auth != null`. No anonymous
  internet access.
- **Web app** signs in with Firebase **Anonymous Authentication** before reading
  or writing; realtime listeners are deferred until sign-in completes.
- **Firmware** authenticates with its own database token, which retains admin
  access and is unaffected by the rules.

### Known limitation

Anonymous auth stops *unauthenticated public* access (and satisfies Firebase's
warning), but anyone who loads the web app still receives an anonymous session
and can control the single shared device. True per-owner isolation requires the
**Phase 1 multi-tenant migration** (`database.rules.target.json`): real user
accounts + per-device namespaces + device auth.

## How to deploy the interim hardening

Order matters — enable the auth provider **before** deploying the rules, or the
web app's anonymous sign-in will fail.

1. **Enable Anonymous Authentication** (Firebase console →
   Authentication → Sign-in method → Anonymous → Enable).
2. **Deploy** the updated rules and web app together:
   ```bash
   firebase deploy --only database,hosting
   ```
3. **Verify**:
   - Open the web app — the status line should show the robot state (not
     "Auth failed"). Send an emotion; the robot should react.
   - Confirm the robot still syncs (it authenticates via its own token, so it
     should be unaffected).

### Rollback

Rules are reversible in seconds. To revert, set `database.rules.json` back to
open rules and `firebase deploy --only database` — but do not leave it open, as
that re-triggers the insecurity warning.

## Outstanding security items (see docs/ROADMAP.md)

- **Rotate** the Firebase database secret that was committed to git history.
- **Scrub** that secret from git history.
- Put **OTA** behind authentication.
- Remove `client->setInsecure()` / verify TLS certificates in firmware.
- Phase 1: full auth + multi-tenant isolation.
