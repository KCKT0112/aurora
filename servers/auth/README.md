# Authentication service

Root-owned ring-3 service with CAP_SESSION. Maps only the private accounts boot
resource, validates its format, and verifies salted PBKDF2-HMAC-SHA256 passwords.
Only display can submit login; current Terminal may request logout. Failed
attempts use a global bounded backoff. Password buffers are erased after use.

On success the kernel establishes a new immutable session credential. Authd
does not spawn programs: init observes the session and starts the apps. Account
creation/password changes currently use the host provisioning tool; see
[security](../../docs/SECURITY.md). This is not a PAM or network-login service.
