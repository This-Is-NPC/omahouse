"""`pacman -U`, and then `pacman -R` — docs/design.md §5.2, "The signing key".

The distance this case closes is the one between "time per site works on the VM"
and "time per site works for whoever installs the package". Until now the `.crx`,
its `updates.xml`, the force-install policy and the native messaging manifest were
put on the machine by hand, by the harness, before any case ran. Somebody typing
`pacman -U omahouse` got none of them, and so got no time per site at all.

Nothing here is mounted by hand. `deploy_on_omarchy` built a package from the
working tree and installed it, the scriptlet of `packaging/omahouse.install` made
this machine's own signing key inside that transaction, signed the meter with it
and wrote the three files that name the id that key produced, and what this case
asserts is that a browser which has never heard of any of it installs the
extension and spawns the host.

**Why a key made during `pacman -U` is the interesting part.**
`.temp/spike-extension.md` §2 proved a `.crx` we signed installs off-store under
`ExtensionSettings` with a `file:` `update_url` — with a key that already existed,
on a machine somebody had prepared. What was never measured is whether a key born
minutes earlier, whose id nothing in the tree can name, is different in any way
Chromium can see. The equality asserted below is the whole answer: the directory
Chromium creates in julia's profile is named with the id derived from the key in
`/etc/omahouse/meter/key.pem`, and the host runs.

**And removal is half the case, not an afterthought.** A machine with no omahouse
on it may not be left with an extension nobody can uninstall, forced by a policy
nothing on the disk can explain — docs/design.md §11 refuses exactly that, and it
would be the worst version of it, because the child would have no Remove button
and the parent would have no program to take it off with. So the second half
removes the package and measures, file by file, that everything is gone: the
policy, the archive, the update manifest, the host manifest, the shim, the key,
the PAM line, the block list and the service.

**It puts the machine back at the end**, by installing the same package again.
That reinstall is a measurement of its own: the key went with `pacman -R`, so the
id that comes back is a **different** one, and that is the cost of having no key
to keep, priced here rather than argued about.
"""

import json
import re
import time
from pathlib import Path

MACHINE = "omarchy"

WHY = "the package alone puts the meter in Chromium, and pacman -R takes all of it out"

#: One site, visited for one window. This case is about the chain arriving
#: through a package; `sites_are_counted_only_with_somebody_there` is where the
#: number itself is measured against the wall clock.
SITE = "example.com/"

def _declared(verb):
    """The one list in `packaging/omahouse.install`, read the way `post_remove`
    reads it.

    It used to be written again in this file, under the name below, and that was
    a third copy of a list that had already been wrong twice -- once in the
    scriptlet, where this case caught it, and once here, where nothing would
    have. The removal half of this case now asks the machine about every line of
    the declaration, so a path added to that file is measured here without
    anybody remembering to come back and add it."""
    text = (Path(__file__).resolve().parents[2] / "packaging/omahouse.install").read_text()
    body = re.search(r"_artifacts\(\)\s*\{\n\s*cat <<-'LIST'\n(.*?)\n\s*LIST\n",
                     text, re.S)
    if not body:
        raise RuntimeError("packaging/omahouse.install no longer has one list to read")
    found = []
    for line in body.group(1).splitlines():
        parts = line.strip().split(None, 2)
        if len(parts) >= 2 and parts[0] == verb:
            found.append(parts[1])
    if not found:
        raise RuntimeError(f"nothing is declared `{verb}` any more")
    return found


#: What `pacman -R` has to take off, out of the scriptlet's own declaration.
TAKEN_ON_REMOVAL = _declared("take")
#: And the directories of ours it empties behind them.
PRUNED_ON_REMOVAL = _declared("prune")

#: What has to be on the machine after `pacman -U`, before a browser has looked
#: at any of it: the four files the scriptlet wrote, the key it wrote them from,
#: and the three the package itself owns.
#:
#: Named here rather than taken from the declaration, and the difference is
#: real. That list is about what removal has to lift, and two of its lines --
#: the `blocked` file and the site policy of §11 -- are written by the running
#: program and are not on a machine that has only just installed this. These are
#: two different questions about overlapping sets, and folding them into one
#: list is how the answer to one of them would quietly become the answer to the
#: other.
PUT_ON_THE_MACHINE = [
    "/etc/chromium/policies/managed/omahouse-meter.json",
    "/etc/chromium/native-messaging-hosts/com.omahouse.meter.json",
    "/usr/share/omahouse/chromium/omahouse-meter.crx",
    "/usr/share/omahouse/chromium/updates.xml",
    "/etc/omahouse/meter/key.pem",
    "/etc/omahouse/meter/id",
    "/usr/lib/omahouse/meter-host",
    "/usr/lib/omahouse/meter-pack",
    "/usr/bin/omahouse",
]


def run(vm):
    Failed = vm.Failed
    julia = vm.subject
    seconds = vm.pace["site_seconds"]
    profile = f"/home/{julia}/.config/chromium/Default"

    def there(path):
        return vm.root(f"test -e {path}", check=False)[0] == 0

    def anything_matching(pattern):
        """The same question for a line that may hold a glob.

        `test -e` cannot be asked one: an unquoted pattern that matches twice
        hands it three arguments and it answers with a usage error, which reads
        as `no`."""
        return vm.root(f"sh -c 'ls -d {pattern} 2>/dev/null | head -1'",
                       check=False)[1].strip() != ""

    def go(where):
        """Ctrl+L, the address, Enter -- the way a person types one."""
        vm.press("29:1 38:1 38:0 29:0")
        time.sleep(1)
        vm.type_text(where)
        vm.press("28:1 28:0")

    # -- 1. what the package left, before a browser has looked at any of it ----

    missing = [path for path in PUT_ON_THE_MACHINE if not there(path)]
    if missing:
        raise Failed("`pacman -U` did not leave these behind, so nothing else here "
                     "means anything:\n      " + "\n      ".join(missing))

    identifier = vm.root("cat /etc/omahouse/meter/id").strip()
    if not identifier or len(identifier) != 32:
        raise Failed(f"/etc/omahouse/meter/id is {identifier!r}")

    # The id in the file is the id of the key, worked out again from the key
    # itself rather than trusted. If these two ever disagreed, the policy would
    # be forcing an extension the archive is not, and the symptom would be a
    # browser silently installing nothing.
    derived = vm.root(
        "sh -c 'openssl rsa -in /etc/omahouse/meter/key.pem -pubout -outform DER "
        "2>/dev/null | openssl dgst -sha256 -r | cut -c1-32 | tr 0-9a-f a-p'").strip()
    if derived != identifier:
        raise Failed(f"the key derives {derived} and /etc/omahouse/meter/id says "
                     f"{identifier}")

    # And the three files the browser reads all name it. Nothing in the tree
    # could have: the id did not exist until the transaction that wrote them.
    for path in ("/etc/chromium/policies/managed/omahouse-meter.json",
                 "/etc/chromium/native-messaging-hosts/com.omahouse.meter.json",
                 "/usr/share/omahouse/chromium/updates.xml"):
        text = vm.root(f"cat {path}")
        if identifier not in text:
            raise Failed(f"{path} does not name {identifier}:\n{text}")

    # The archive is owned by no package, and that is the fact rather than an
    # accident: it did not exist when the package was built, because the key that
    # decides its contents did not either. It is why `post_remove` removes it by
    # hand and why this case checks by hand that it did.
    owner = vm.root("pacman -Qo /usr/share/omahouse/chromium/omahouse-meter.crx",
                    check=False)[1].strip()
    if "No package owns" not in owner:
        raise Failed(f"the archive is a packaged file after all: {owner}")
    print(f"      extension id on this machine  {identifier}")
    print("      the archive itself            written by the scriptlet, owned by "
          "no package")

    # -- 2. a browser that has never heard of any of it -----------------------

    if not vm.pid_of("chromium"):
        vm.press("125:1 42:1 48:1 48:0 42:0 125:0")
        vm.wait_for(lambda: bool(vm.pid_of("chromium")), vm.pace["patience_seconds"],
                    "Chromium to open")
        time.sleep(5)

    # The whole chain in one fact: the policy was read, the `file:` update URL was
    # fetched, the `.crx` verified against an id derived from a key made during a
    # pacman transaction, the extension installed, its service worker started, and
    # Chromium spawned the shim as julia.
    #
    # Asked as `pgrep -u julia -x omahouse` and **never** as
    # `pgrep -f 'omahouse meter'`. The first version of this case used the second
    # and it is an assertion that cannot fail: the harness reaches the guest over
    # ssh, sshd runs the command inside a shell, and that shell's own command line
    # holds the words being searched for. It answered a pid every time -- its own
    # -- and the first run of this case failed on exactly that pid after the
    # removal, reporting a host that had never existed. `-x` matches the process
    # name and `-u` the account, and neither can be satisfied by the question.
    def host_pids():
        return vm.root(f"pgrep -u {julia} -x omahouse || true", check=False)[1].split()

    vm.wait_for(lambda: bool(host_pids()), vm.pace["patience_seconds"],
                "the meter's native messaging host to be spawned by the browser")
    running = vm.root(f"ps -o user=,args= -p {' -p '.join(host_pids())}",
                      check=False)[1].strip()
    if julia not in running or "meter" not in running:
        raise Failed(f"what is running as {julia} is not the meter: {running!r}")
    print(f"      the host Chromium spawned     {running}")

    # The directory Chromium made for it, named with the id it worked out of the
    # archive's own signature. This is the equality the case exists for: a key
    # that did not exist before the install produced an id the browser accepts.
    installed = vm.root(f"ls {profile}/Extensions 2>/dev/null || true", check=False)[1]
    if identifier not in installed.split():
        raise Failed(f"Chromium installed {installed.split()} and not {identifier}")
    version = vm.root(f"ls {profile}/Extensions/{identifier}", check=False)[1].split()
    print(f"      Chromium installed            {identifier} {' '.join(version)}")

    # -- 3. and the site is counted -------------------------------------------

    day = f"/var/lib/omahouse/{julia}/{vm.today()}.json"
    go("about:blank")
    time.sleep(3)
    vm.stop_daemon()
    vm.root(f"rm -f {day}")
    for what in ("--session 180m", "--budget chromium=180m",
                 "--budget org.chromium.Chromium=180m"):
        vm.root(f"omahouse grant {julia} {what}")
    vm.start_daemon()
    time.sleep(1)

    began = time.time()
    go(SITE)
    time.sleep(seconds)
    vm.stop_daemon()
    watched = time.time() - began

    written = json.loads(vm.root(f"cat {day}"))
    name = SITE.split("/")[0]
    counted = written.get("sites", {}).get(name, 0)
    # A window and not a number: a page takes a moment to load and be reported,
    # and the daemon ticks every two seconds. What a broken chain looks like here
    # is zero, not two seconds short.
    if counted < watched - 6:
        raise Failed(f"{name} was in front for {watched:.0f}s and the day counted "
                     f"{counted}s of it. The whole day: {written.get('sites')}")
    report = vm.root(f"omahouse report {julia}")
    print(f"      {name} was in front for {watched:.0f}s and the report says {counted}s")
    print("      " + report.replace("\n", "\n      "))

    # -- 4. pacman -R ---------------------------------------------------------

    pam_before = vm.root("grep -c omahouse /etc/pam.d/system-login || true",
                         check=False)[1].strip()
    # The host's pid, kept across the removal. A native messaging host is spawned
    # by the browser and lives as long as the pipe does, so with Chromium still
    # running nothing but `post_remove` can end it -- which makes "this pid is
    # gone and Chromium is not" a measurement of the kill and not of a coincidence.
    was_hosting = host_pids()
    if not was_hosting:
        raise Failed("the host had already gone before the removal, so nothing here "
                     "can say whether `pacman -R` would have ended it")
    code, said = vm.root("pacman -R --noconfirm omahouse", check=False)
    if code != 0:
        raise Failed(f"pacman -R said:\n{said}")
    print("      pacman -R:")
    for line in said.splitlines():
        if line.startswith(">>>"):
            print("        " + line.strip())

    # -- 5. and everything is off the machine ---------------------------------

    # Everything this case saw arrive, and everything the scriptlet declares it
    # takes -- which is a longer list, because it also covers what the *running*
    # program leaves behind and this machine may or may not have.
    asked = PUT_ON_THE_MACHINE + [path for path in TAKEN_ON_REMOVAL
                                  if path not in PUT_ON_THE_MACHINE]
    left = [path for path in asked if anything_matching(path)]
    if left:
        raise Failed("`pacman -R` left these on a machine with no omahouse on it:\n"
                     "      " + "\n      ".join(left))
    print(f"      all {len(asked)} of them are gone, and the list came from "
          "packaging/omahouse.install")

    # The directories, not only the files: an empty directory is not a
    # restriction, but one that is still there is a sign the removal ran half
    # way. `/usr/lib/omahouse` is pacman's own and the rest are declared.
    for directory in PRUNED_ON_REMOVAL + ["/usr/lib/omahouse"]:
        if anything_matching(directory):
            raise Failed(f"{directory} is still on the machine after pacman -R")

    # Nothing under the browser's own directories mentions omahouse -- and the
    # directories themselves are still there, because they were not ours to take.
    stray = vm.root("sh -c 'grep -rl omahouse /etc/chromium 2>/dev/null || true'",
                    check=False)[1].strip()
    if stray:
        raise Failed(f"/etc/chromium still mentions omahouse:\n{stray}")
    listing = vm.root("sh -c 'ls -A /etc/chromium/policies/managed "
                      "/etc/chromium/native-messaging-hosts 2>&1'", check=False)[1]
    print("      what is left under /etc/chromium:")
    print("        " + (listing.strip() or "(nothing)").replace("\n", "\n        "))

    # The PAM line, the block list and the service -- the rest of what a removal
    # has to lift, checked here because a browser extension left behind and a
    # login left refused are the same mistake with a different file.
    if vm.root("grep -q 'file=/etc/omahouse/blocked' /etc/pam.d/system-login",
               check=False)[0] == 0:
        raise Failed("the PAM line is still in /etc/pam.d/system-login")
    if there("/etc/omahouse/blocked"):
        raise Failed("/etc/omahouse/blocked is still there, so somebody may still be "
                     "locked out by a program that is gone")
    active = vm.root("systemctl is-enabled omahouse.service 2>&1 || true",
                     check=False)[1].strip()
    print(f"      pam lines before {pam_before}, after 0 · omahouse.service {active}")

    # The host the browser had spawned, ended by the removal itself and not by
    # anything this case did. Chromium was still running at that moment -- checked
    # before the pid is -- so the pipe was still open and nothing but `post_remove`
    # could have closed it. Without that kill it is julia's process, holding a
    # deleted binary, appending sites she visits to a file the same `post_remove`
    # had just deleted, on a machine with nothing left on it to explain either.
    if not vm.pid_of("chromium"):
        raise Failed("Chromium closed during the removal, so the host dying proves "
                     "nothing about post_remove having ended it")
    for pid in was_hosting:
        if there(f"/proc/{pid}"):
            raise Failed(f"the host at pid {pid} outlived `pacman -R` with Chromium "
                         "still open")
    print(f"      the host at {was_hosting} died with the removal, Chromium still open")

    # And the browser, once it is started again with no policy naming anything.
    # A force-installed extension is removed when the policy stops naming it; if
    # this ever changed, a household would be left with an extension the child
    # cannot remove and the parent has no program to take off.
    vm.root(f"pkill -u {julia} -x chromium || true", check=False)
    time.sleep(3)
    vm.press("125:1 42:1 48:1 48:0 42:0 125:0")
    vm.wait_for(lambda: bool(vm.pid_of("chromium")), vm.pace["patience_seconds"],
                "Chromium to open again with no omahouse on the machine")
    time.sleep(8)
    still = vm.root(f"ls {profile}/Extensions 2>/dev/null || true", check=False)[1].split()
    left_hosting = host_pids()
    print(f"      Chromium's extensions now     {still or '(none)'}")
    print(f"      hosts as {julia} now        {left_hosting or '(none)'}")
    if identifier in still:
        raise Failed(f"{identifier} is still installed in {julia}'s profile after the "
                     "policy that forced it was removed")
    if left_hosting:
        raise Failed(f"something is still running as the meter's host: {left_hosting}\n"
                     "      It is julia's process, it holds a deleted binary, and it "
                     "goes on writing the file that names the sites she visits.")

    # -- 6. and the machine put back, which prices the reinstall ---------------

    vm.root(f"pkill -u {julia} -x chromium || true", check=False)
    vm.install_the_package()
    again = vm.root("cat /etc/omahouse/meter/id").strip()
    if not again or len(again) != 32:
        raise Failed(f"the reinstall wrote no id: {again!r}")
    if again == identifier:
        raise Failed("the reinstall produced the same extension id, which means the "
                     "key survived `pacman -R` -- and a key that outlives its own "
                     "program is the one artefact this design exists not to have")
    print(f"      reinstalled, and the id moved  {identifier} -> {again}")
