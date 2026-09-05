#!/usr/bin/env bash
# Sign the meter extension into a `.crx`, and write the `updates.xml` beside it.
#
#     extension/pack.sh [destination]
#
# With no destination it writes into a temporary directory and prints where. It
# never writes into the tree: a signed archive is a build artefact, and one
# committed beside its own source is one nobody can tell is stale.
#
# -- the key ------------------------------------------------------------------
#
# `docs/proposal-browser.md` §6.3 named this as a cost of self-hosting and did
# not decide it. It is decided here.
#
# **In development it lives at `~/.config/omahouse/omahouse-meter.pem`**, outside
# the repository, 0600, and this script makes one there the first time it is run
# if there is not one already. `$OMAHOUSE_EXTENSION_KEY` moves it. It is never
# written into the working tree and `.gitignore` refuses `*.pem` besides, because
# the failure here is silent: a private key committed once is a private key
# somebody else can sign a force-installed extension with, on every machine that
# has the policy.
#
# **The public half is committed**, in `extension/manifest.json` under `key`.
# That is what fixes the extension id -- `ghiofeehkpcmfcpogcpjlpnfgnfaidjn` --
# across rebuilds, so `packaging/omahouse-meter-policy.json` can name it
# statically. A public key in a repository is not a secret and never was; what it
# buys is that a rebuilt package installs over the one before it rather than
# beside it under a new name.
#
# **In production it would not live on a developer's machine at all.** The same
# shape as any package signing key: held by whoever cuts releases, in a
# passphrase-protected file or a hardware token, and reachable by the release
# job and nothing else. Two properties matter and neither is about convenience --
# losing it means every installed policy names an extension that no longer
# exists, and leaking it means somebody else can sign something those policies
# will force-install. Until this project cuts releases there is nothing to
# decide beyond writing that down, and the development key above is explicitly
# not it: it signs a `.crx` for a VM and for a laptop, and a machine that took a
# real one would be taking a key four people have a copy of.
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
key="${OMAHOUSE_EXTENSION_KEY:-$HOME/.config/omahouse/omahouse-meter.pem}"
destination="${1:-$(mktemp -d /tmp/omahouse-meter.XXXXXX)}"

browser=""
for candidate in chromium chromium-browser google-chrome-stable google-chrome; do
    if command -v "$candidate" >/dev/null 2>&1; then
        browser="$candidate"
        break
    fi
done
if [ -z "$browser" ]; then
    echo "pack.sh: no chromium on this machine, and packing a .crx needs one." >&2
    echo "         The VM has one; so does any machine with the browser this" >&2
    echo "         extension is for." >&2
    exit 1
fi

if [ ! -f "$key" ]; then
    mkdir -p "$(dirname "$key")"
    ( umask 077; openssl genrsa -out "$key" 2048 2>/dev/null )
    echo "pack.sh: made a development key at $key" >&2
    echo "         The public half in extension/manifest.json is what fixes the" >&2
    echo "         id, so a key made here now will NOT match it. Regenerate the" >&2
    echo "         manifest's \`key\` field from this one, or put the original" >&2
    echo "         key back." >&2
fi

mkdir -p "$destination"
work="$(mktemp -d /tmp/omahouse-meter-pack.XXXXXX)"
trap 'rm -rf "$work"' EXIT

# A copy, because `--pack-extension` writes `omahouse-meter.crx` beside the
# directory it was given and would otherwise drop a build artefact into the tree.
cp "$here/manifest.json" "$here/background.js" "$work/"
mkdir -p "$work/src"
mv "$work/manifest.json" "$work/background.js" "$work/src/"

# A user data directory of its own, every time, thrown away at the end. Packing
# an extension is not something to do inside somebody's own browser profile, and
# this script runs on the machine the developer reads their mail on.
"$browser" \
    --user-data-dir="$work/profile" \
    --no-first-run \
    --disable-gpu \
    --pack-extension="$work/src" \
    --pack-extension-key="$key" >/dev/null 2>&1

crx="$work/src.crx"
[ -f "$crx" ] || { echo "pack.sh: $browser wrote no .crx" >&2; exit 1; }

version="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["version"])' \
    "$here/manifest.json")"
id="$(python3 - "$key" <<'PYTHON'
import base64, hashlib, subprocess, sys
der = subprocess.run(["openssl", "rsa", "-in", sys.argv[1], "-pubout", "-outform", "DER"],
                     capture_output=True, check=True).stdout
digest = hashlib.sha256(der).hexdigest()[:32]
print("".join(chr(ord("a") + int(c, 16)) for c in digest))
PYTHON
)"

install -Dm644 "$crx" "$destination/omahouse-meter.crx"

# The `file:` update manifest. `.temp/spike-extension.md` §2 measured this whole
# chain on real Omarchy: a `.crx` under `/usr/share`, an `updates.xml` naming it
# by `file:`, and `ExtensionSettings` forcing it -- it installed on the first
# start of the browser, it updated when the version went up, and the child had no
# Remove button and a toggle that would not move.
cat > "$destination/updates.xml" <<XML
<?xml version='1.0' encoding='UTF-8'?>
<gupdate xmlns='http://www.google.com/update2/response' protocol='2.0'>
  <app appid='$id'>
    <updatecheck codebase='file:///usr/share/omahouse/chromium/omahouse-meter.crx'
                 version='$version' />
  </app>
</gupdate>
XML

echo "crx      $destination/omahouse-meter.crx"
echo "updates  $destination/updates.xml"
echo "id       $id"
echo "version  $version"
