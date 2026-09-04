"""`logout` is two things, and this is the case that says why -- docs/design.md §2.

docs/design.md round 2 measured `loginctl terminate-user` on this very machine:
the sessions went down in seconds, and the tty1 autologin put them straight back
up. On a household machine that is the ordinary case, and it makes
`onExhausted: "logout"` theatre.

Round 3 measured the fix, and it is a stock PAM module and no code of ours. So
what is asserted here is all three parts of it:

  1. the name goes into /etc/omahouse/blocked and the session ends;
  2. the autologin comes back and is **refused**, by `pam_listfile`, in the
     journal, with the session staying down;
  3. giving the time back takes the name out, and the session returns on its own
     with nobody typing anything.

The third is the one that makes the first safe. A block that only a person can
lift is a person locked out of their own machine at midnight.
"""

import time

WHY = "the session ends, the autologin is refused, and giving time back lets them in"


def run(vm):
    Failed = vm.Failed
    # The PAM line has to be in the stack, or none of this means anything. It is
    # the packaging's job -- omahouse.install put it there -- and it is checked
    # here rather than assumed, because a green run over a stack without it would
    # be the worst possible result.
    stack = vm.ssh("cat /etc/pam.d/system-login")
    if "pam_listfile.so" not in stack or "/etc/omahouse/blocked" not in stack:
        raise Failed("the PAM line of docs/design.md §2 is not in /etc/pam.d/system-login")
    if "onerr=succeed" not in stack:
        raise Failed("the PAM line has no onerr=succeed, which would lock this machine "
                     "out of itself the moment /etc/omahouse/blocked went missing")

    sessions_before = vm.sessions()
    if not sessions_before:
        raise Failed("there is no session to end")

    # The session budget is the one whose selector is `*`, so it only runs while
    # something is open. An app first, then the clock means something.
    vm.launch(args="900")

    vm.make_profile(budgets={"session": 1}, default="allow",
                    grace=vm.manifest["budgets"]["grace_seconds"], warn_at=(1,))
    vm.seed_ledger({"session": 60 - vm.manifest["budgets"]["session_seconds"]})

    marker = vm.ssh("date '+%Y-%m-%d %H:%M:%S'").strip()
    vm.start_daemon()

    # 1. The name in the file, and the sessions gone.
    vm.wait_for(lambda: vm.subject in vm.blocked(), 90,
                f"{vm.subject} to be written into /etc/omahouse/blocked")
    blocked_at = time.time()
    vm.wait_for(lambda: not vm.sessions(), 60, "the session to end")
    print(f"      blocked, then {sessions_before} ended "
          f"{time.time() - blocked_at:.0f}s later")

    # 2. And it stays down. The tty1 autologin fires again within seconds, and
    #    what has to happen is that PAM refuses it -- not that nothing tried.
    stayed_down_until = time.time() + 25
    while time.time() < stayed_down_until:
        if vm.sessions():
            raise Failed(f"{vm.subject} logged straight back in: {vm.sessions()}")
        time.sleep(2)

    refusals = vm.root(
        f"journalctl --since '{marker}' --no-pager "
        f"| grep 'pam_listfile.*Refused user {vm.subject}' || true", check=False)[1]
    if not refusals.strip():
        raise Failed("nothing in the journal shows pam_listfile refusing the autologin, "
                     "so the session may only be down because nothing tried. The whole "
                     "of what the block did:\n"
                     + vm.root(f"journalctl --since '{marker}' --no-pager | "
                               "grep -i pam_listfile || true", check=False)[1])
    for line in refusals.strip().splitlines()[:2]:
        print("      " + line.strip())
    print(f"      ({len(refusals.strip().splitlines())} refusals in {round(time.time() - blocked_at)}s "
          "-- the autologin kept trying, and PAM kept saying no)")

    if vm.subject not in vm.blocked():
        raise Failed("the name came out of the file while the budget was still spent")

    # 3. The operator hands over ten minutes with the machine still shut --
    #    docs/design.md §1 -- and nothing else has to be done.
    vm.root(f"omahouse grant {vm.subject} --session 10m")
    vm.wait_for(lambda: vm.subject not in vm.blocked(), 30,
                f"{vm.subject} to come out of /etc/omahouse/blocked")
    asked = time.time()
    vm.wait_for(lambda: bool(vm.sessions()), 90,
                "the autologin to be let through again")
    print(f"      granted 10m; unblocked, and the session came back on its own "
          f"{time.time() - asked:.0f}s later as {vm.sessions()}")

    # And it is a real session again, not a shell that was refused halfway.
    state = vm.ssh(f"loginctl show-session {vm.sessions()[-1]} -p State --value").strip()
    if state not in ("active", "online", "opening"):
        raise Failed(f"the session came back {state!r}")
