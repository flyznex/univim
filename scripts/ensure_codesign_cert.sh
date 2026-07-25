#!/bin/sh
# Ensures a stable, local self-signed code-signing identity exists in the
# current user's login keychain, creating one if missing. Every machine
# that builds UniVim gets its own independent certificate -- the point
# isn't a shared/trusted identity, just a *consistent* one so `codesign`
# with the same name always produces the same signing identity across
# rebuilds on that machine. Without this, the linker's default ad-hoc
# signature hashes the binary's own content, so every rebuild looks like a
# "different app" to TCC and Accessibility permission has to be re-granted
# every time.
set -e

CERT_NAME="${1:?usage: ensure_codesign_cert.sh <cert-name>}"

# Resolve the real login keychain via Directory Services, not $HOME --
# Homebrew's sandboxed build runs with an isolated fake $HOME with no
# keychain at $HOME/Library/Keychains/login.keychain-db, and `security`
# actually does resolve its *default* keychain relative to $HOME too, so
# omitting -k doesn't help either: it fails to import with no error output
# at all under a fake $HOME (confirmed directly). dscl's NFSHomeDirectory
# reflects the real account home directory regardless of what $HOME the
# calling process happens to have (confirmed the same way).
REAL_HOME=$(dscl . -read "/Users/$(whoami)" NFSHomeDirectory | awk '{print $2}')
KEYCHAIN="$REAL_HOME/Library/Keychains/login.keychain-db"

if security find-identity -v -p codesigning "$KEYCHAIN" 2>/dev/null | grep -q "\"$CERT_NAME\""; then
  exit 0
fi

echo "No '$CERT_NAME' code-signing identity found -- creating one"

TMPDIR=$(mktemp -d)
trap 'rm -rf "$TMPDIR"' EXIT

cat > "$TMPDIR/cert.conf" <<EOF
[req]
distinguished_name = req_dn
x509_extensions = v3_req
prompt = no

[req_dn]
CN = $CERT_NAME

[v3_req]
keyUsage = critical, digitalSignature
extendedKeyUsage = critical, codeSigning
EOF

openssl req -x509 -newkey rsa:2048 -keyout "$TMPDIR/key.pem" -out "$TMPDIR/cert.pem" \
  -days 3650 -nodes -config "$TMPDIR/cert.conf" -sha256

# OpenSSL 3.x defaults PKCS12 to AES/PBES2 encryption, which macOS's
# Security framework can't parse ("MAC verification failed" on import) --
# needs the legacy RC2/3DES format `security import` understands, via
# OpenSSL 3.x's own -legacy flag. LibreSSL/OpenSSL 1.x (what Homebrew's
# sandboxed build uses, via /usr/bin/openssl) predates the provider system
# -legacy selects and doesn't have the flag at all, but already emits a
# compatible format by default -- so only pass -legacy when supported.
# An empty -passout also triggers the same MAC-verification failure
# independently of the encryption format (confirmed by testing each
# combination directly), so a fixed placeholder password is used instead --
# it's not a real secret, just a required non-empty PKCS12 transport value
# for this local, non-interactive import.
PKCS12_PASSWORD="univim-local-cert"
LEGACY_FLAG=""
if openssl pkcs12 -help 2>&1 | grep -q -- -legacy; then
  LEGACY_FLAG="-legacy"
fi
openssl pkcs12 -export $LEGACY_FLAG -out "$TMPDIR/cert.p12" \
  -inkey "$TMPDIR/key.pem" -in "$TMPDIR/cert.pem" -passout "pass:$PKCS12_PASSWORD"

# -T /usr/bin/codesign lets codesign use the private key without a
# keychain-unlock prompt on every build.
security import "$TMPDIR/cert.p12" -k "$KEYCHAIN" -P "$PKCS12_PASSWORD" -T /usr/bin/codesign -A

# Trust scoped to this login keychain only (no sudo/System keychain needed)
# -- sufficient for codesign to treat it as a valid signing identity.
security add-trusted-cert -p codeSign -k "$KEYCHAIN" "$TMPDIR/cert.pem"

echo "Created and trusted '$CERT_NAME' for code signing."
