# How to install omahouse, and how to take it off again

**The question:** how do I get this onto a machine, what does it put there, and
what happens to all of it when I remove the package?

This page is enough on its own. It is deliberately as long about the removal as
about the install: **a restriction that outlives the program that made it is
worse than one that never existed**, because nothing left on the disk knows how
to lift it.

---

## What it needs

- **Omarchy**, or an Arch machine with `uwsm`, systemd and `logind`. Half of
  omahouse is `logind`, a real session and a real seat, so a container without
  systemd as PID 1 is not a place it runs.
- **Qt 6** — `qt6-base`, and `qt6-declarative` for the window.
- **`polkit`**, for `pkexec`. That is how the window writes.
- **Chromium**, for anything to do with sites. It is optional; everything else
  works without a browser.
- [**mise**](https://mise.jdx.dev/), for the tasks below and for the pinned
  `usage` that generates [the command line reference](cli.md).

---

## From a checkout

Not published as an Omarchy default yet, so this is the honest path today:

```bash
mise run deps      # checks the system Qt/qmake toolchain
mise run build     # build/bin/omahouse and build/bin/omahouse-studio
mise run studio    # open the window
```

Shadow build only — qmake refuses to configure inside the source tree.

**A checkout is not an installation.** Nothing is enabled, no PAM line is
written, no service runs, and there is no browser meter, so nothing counts time
per site. It is enough to read a machine with (`omahouse status`) and to try a
cycle with (`omahouse watch --once --dry-run`), and not enough for the rules to
hold.

Every root the program reads moves by environment variable, which is how the end
to end suite runs as an ordinary user with nothing installed:

```bash
export OMAHOUSE_CONFIG_DIR=/tmp/omahouse/etc
export OMAHOUSE_STATE_DIR=/tmp/omahouse/var
build/bin/omahouse profile add nobody --name "Kid"
build/bin/omahouse status nobody
```

Two of those roots also keep a run pointed somewhere else from biting: a `close`
is refused unless the cgroup tree really is `/sys/fs/cgroup`, a `terminate-user`
is refused unless the configuration really is `/etc/omahouse`, and the machine's
own Chromium policy is only written by a run that is also managing the machine's
own `/etc/omahouse`.

## As a package

The Arch recipe lives in the sibling `omarchy-pkgs` repository, and
`packaging/` here holds what it installs: the systemd unit, the polkit policy,
the desktop entry, the icon, the native messaging host shim, the extension
packer and the `.install` scriptlet.

```
>>> omahouse: /etc/omahouse and /var/lib/omahouse are ready, and
>>>           omahouse.service is enabled for the next boot.
>>>
>>>           Nobody is under rules yet. Start with:
>>>             omahouse profile add <user> --name "Name"
>>>             omahouse status <user>
>>>
>>>           A new profile only counts and reports. Read a day of
>>>           `omahouse report <user>` before `omahouse profile enforce
>>>           <user> --on` puts the teeth in.
```

The service is **enabled and not started**: a daemon that begins counting inside
a `pacman` transaction is a daemon nobody chose to run yet, and there are no
profiles on a machine that has just installed this. It starts at the next boot,
or when somebody asks. `Restart=always` matters — a stopped daemon is a rule
switched off, and an account without privilege cannot stop a system service.

---

## What the install puts on the machine

Most of it is not a file the package owns, and that is exactly why the removal
is half the job.

| what | where | why it is not just a file |
|---|---|---|
| the login block | one line of stock `pam_listfile` in `/etc/pam.d/system-login` | it reads `/etc/omahouse/blocked`, and a name left there with no omahouse on the disk locks somebody out of their own machine |
| the list it reads | `/etc/omahouse/blocked` | written afresh every cycle from what is true right now |
| the browser policy | `/etc/chromium/policies/managed/omahouse.json` | left behind, it is a site that will not open and a browser saying *managed by your organisation* with nobody to ask |
| the browser meter | a `.crx`, an `updates.xml`, a force-install policy, a native messaging manifest, and a signing key | none of them is shipped; all five are **made on the machine** by the install scriptlet |
| the service | `omahouse.service`, enabled | a rule that comes back at boot |

**The extension arrives with the package and leaves with it.** The scriptlet
makes an RSA key on your machine, signs the extension into a `.crx` with it, and
forces that into Chromium through a `file:` update URL. There is nothing to
download, no store account, and no signing key anybody has to keep — because the
one that signs yours cannot sign anything anybody else's browser would accept:

```
>>> omahouse: the browser meter is signed for this machine and forced
>>>           into Chromium. Its extension id here is ahdeiepgabnoenfnoebeomaoipdfgkpl
```

That id is per machine, and **no file in this repository contains it**.
Everything that needs it reads `/etc/omahouse/meter/id`.

- **A reinstall makes a different extension.** The removal deletes the key, so
  installing again gives a new key, a new id, and an extension Chromium has
  never seen. That is affordable only because this extension keeps no state at
  all — no storage permission, no options, nothing saved per site.
- **An upgrade is not a reinstall.** `post_upgrade` finds the key and re-signs
  the same id at the new version, which the browser takes as an update. The key
  surviving an upgrade and not surviving a removal is the whole design in one
  line.
- **If signing fails**, the transaction still succeeds and says so, naming what
  is missing. What is lost is time per site; everything else works.

---

## Taking it off

```bash
sudo pacman -R omahouse
```

```
>>> omahouse: everything omahouse put on this machine is off it.
>>>
>>>           The login block is gone and everybody can log in again, the
>>>           browser policy in /etc/chromium/policies/managed/omahouse.json
>>>           is gone and every site opens again, the meter extension is no
>>>           longer force-installed, its native host is gone, the key this
>>>           machine signed it with is gone, and the service is stopped and
>>>           disabled. No rule omahouse wrote is still in force.
>>>
>>>           Chromium drops a force-installed extension the next time it
>>>           starts without a policy naming it. If a window was open through
>>>           all of this, close it once.
>>>
>>>           Kept on purpose, and here is where they are:
>>>             /etc/omahouse                 the directory the two below live in
>>>             /etc/omahouse/profiles.json   who was under rules
>>>             /var/lib/omahouse             the days already counted
>>>           A report is evidence, and it outlives the rules it was
>>>           collected under. Nothing there does anything to the machine
>>>           now; `rm -rf` all three if you want the account back to nothing.
```

**One thing that is removed and is not a file.** A native messaging host lives
as long as the pipe the browser gave it, so a `pacman -R` with a Chromium window
open would otherwise leave `/usr/bin/omahouse meter` running as the child,
holding a binary that no longer has a name, appending sites to a file that had
just been deleted — and the file comes back within five seconds on a machine
with no omahouse on it and nothing left to explain it. The removal ends that
process by its exact command line, and the VM case measures that it was the
removal which did so.

**On a computer that was linked to a household, the link comes off too.** That
is a second set of things, and no install put any of them there — `omahouse
machine link` writes them onto a computer long after the package went on it:

```
>>>           This computer was linked to a household, and it is not any
>>>           more. The sudoers line is gone, the bearer tokens are gone,
>>>           and it no longer says it is managed by anybody. The console
>>>           this household reached it on is closed.
```

**Omakure is left as it was**, and that is deliberate: it is its own program,
and taking omahouse off is not a reason to take a household's wire down. If its
node was started by omahouse it is stopped; if the node is Omakure's own it goes
on running, on loopback, with the drop-in that had opened it to the household
removed.

**The manager still has that computer in its list.** Removing the package there
cannot reach here. Run `sudo omahouse machine remove "<name>"` on the manager,
or its sums go on waiting for a day that computer will never send.

**None of this is a list somebody remembers to keep up to date.** The scriptlet
declares what an installation puts on a machine, the removal is a loop over that
declaration, and a test in `mise run verify` installs and removes the whole
thing against a temporary directory on every commit — refusing a commit where
something appeared that the declaration does not name, or survived a removal
that should have taken it. [`design.md` §9](design.md) is the long version.

## Taking a rule off without uninstalling

You do not have to remove the package to undo anything:

- `sudo omahouse profile remove kid` takes one profile off the books, with its
  web rules, and leaves the account and the days already counted alone.
- Taking back the last block on the machine **removes** the browser policy file
  rather than emptying it.
- `sudo omahouse profile enforce kid --off` leaves everything written and stops
  it biting.

---

## What can go wrong

**`/etc/pam.d/system-login` is not there.** The scriptlet says so and installs
the rest. Without that line, `logout` ends a session that an autologin brings
straight back up.

**The PAM file was replaced by a `.pacnew` merge.** The line goes back on every
upgrade for exactly that reason: a machine where the block quietly went away is
a machine that looks fiscalised and is not.

**`openssl` or `zip` is missing.** No meter, said out loud, with the command to
run once they are there.

**A Chromium window was open through the removal.** Chromium drops a
force-installed extension the next time it starts without a policy naming it.
Close it once.

---

## Next

- [How to put an account under rules](how-to-put-an-account-under-rules.md) —
  the first thing to do on a fresh install.
- [How it is built](design.md) — the model, the measurements, and the gate.
