"""A real node scheduler collects, plans and applies without a manual sync run.

Two disposable guests, the actual Battery and HTTP API. What is proved is the
thing portions could not do: **time spent on one computer comes off the other
computer's balance**, without anybody dividing anything. Repeated fires and a
manager restart cannot refill the credit, and a machine that stops reporting
stops moving the balance rather than reserving a share of it.
"""
import importlib.util
import json
import shlex
import time
from pathlib import Path

MACHINE = 'poc'
WHY = 'the Omakure scheduler runs the Battery and every machine sees one balance'


def run(vm):
    dad = vm.peer('dad')
    # Reuse the existing, independently asserted pairing/transport setup.
    path = Path(__file__).with_name('a_day_travels_between_two_machines.py')
    spec = importlib.util.spec_from_file_location('collection_reference', path)
    setup = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(setup)
    dad.put(str(vm.build_binary), '/tmp/omahouse')
    dad.root('install -Dm755 /tmp/omahouse /usr/bin/omahouse')
    try:
        setup.collected(vm, dad)
        exercise(vm, dad)
    finally:
        dad.root('rm -f /usr/bin/omahouse', check=False)


def allocation_on(box, child, json_cli):
    """That machine's statement, out of `profile show`.

    `profile show --json` answers in the envelope `profiles.json` uses -- a
    `schemaVersion` and a list -- because the bytes it emits are the bytes
    `profile apply-staged` takes in on another computer. So the profile is one
    level down, and reaching for the field directly is how this case came to
    fail on a shape that had changed under it: the envelope arrived with the
    verbs that push a profile between computers, and nothing here ran again
    until now. The failure named `allocation` and pointed at the household,
    which had nothing to do with it.
    """
    profiles = json_cli(box, 'profile', 'show', child)['profiles']
    if len(profiles) != 1:
        raise box.Failed(f'{box.machine} shows {len(profiles)} profiles for {child}')
    return profiles[0].get('allocation')


def exercise(vm, dad):
    child = vm.subject
    workspace = '/var/lib/omakure-workspace'
    def omk(box, *args):
        return box.root('-u omakure env HOME=' + workspace + ' OMAKURE_SCRIPTS_DIR=' + workspace
                        + ' omakure ' + ' '.join(shlex.quote(a) for a in args))
    def json_cli(box, *args):
        return json.loads(box.root('omahouse --json ' + ' '.join(shlex.quote(a) for a in args)))

    for box in (vm, dad):
        for script in ('omahouse.allocation', 'omahouse.sync'):
            omk(box, 'battery', 'install', 'omahouse', script)
    vm.root('systemctl restart omakure-node.service')
    # A bounded explicit enrollment, then every subsequent sync must be a
    # Scheduled row, not a manual command masquerading as automation.
    enrolled = dad.root('-u omakure env OMAKURE_SCRIPTS_DIR=' + workspace + ' python3 '
                        + workspace + '/omahouse-sync.py --action enroll --users ' + shlex.quote(child))
    result = json.loads(enrolled)
    if not result['ok']:
        raise vm.Failed(f'enrollment did not finish: {result}')
    initial = json_cli(dad, 'allocation', 'show', child)['reservation']
    if len(initial['documents']) != 2:
        raise vm.Failed(f'expected a statement per machine: {initial}')
    # One credit, told to both, and not a share each. Summing these would be the
    # old question; the new one is whether they agree.
    credits = {d['house']['session']['credit'] for d in initial['documents'].values()}
    if credits != {7200}:
        raise vm.Failed(f'machines were told different credits: {credits}')
    original_remote = allocation_on(vm, child, json_cli)
    dad.put(str(dad.battery_checkout / '.scripts/configure-sync.py'), '/tmp/configure-sync.py')
    dad.root('-u omakure python3 /tmp/configure-sync.py --workspace ' + workspace
             + ' --users ' + shlex.quote(child) + ' --cron ' + shlex.quote('*/10 * * * * *'))
    dad.root('systemctl restart omakure-node.service')
    def completed():
        data = json.loads(omk(dad, '--json', 'history', 'list', '--limit', '100'))['data']
        rows = data.get('runs', data.get('items', [])) if isinstance(data, dict) else data
        return [r for r in rows if r.get('script_path', '').endswith('omahouse-schedule.py')
                and r.get('trigger') in ('Scheduled', 'scheduled')
                and r.get('state') in ('completed', 'failed')]
    def wait_rows(count, ok=None):
        deadline = time.monotonic() + 150
        while time.monotonic() < deadline:
            rows = completed()
            matching = rows if ok is None else [r for r in rows if (r.get('exit_code') == 0) == ok]
            if len(matching) >= count:
                return matching
            time.sleep(2)
        raise vm.Failed(f'scheduler did not produce {count} completed rows: {completed()}')
    first = wait_rows(2, True)
    if json_cli(dad, 'allocation', 'show', child)['reservation'] != initial:
        raise vm.Failed('repeated scheduled fires changed reserved credit')
    print(f'      {len(first)} actual scheduled runs completed; the credit is still 7200s '
          'on both')
    dad.root('systemctl restart omakure-node.service')
    wait_rows(3, True)
    if allocation_on(vm, child, json_cli) != original_remote:
        raise vm.Failed('manager restart recharged the remote machine')
    vm.root('systemctl stop omakure-node.service')
    wait_rows(1, False)
    if json_cli(dad, 'allocation', 'show', child)['reservation'] != initial:
        raise vm.Failed('a machine that stopped reporting changed the household balance')
    vm.root('systemctl start omakure-node.service')

    # Ten minutes are ten minutes on the machine they were typed on, before any
    # manager hears about it. This used to be the one thing an enrolled profile
    # would not do: the balance came from the statement, a grant was in none of
    # its numbers, and `grant` printed the same figure back and changed nothing
    # until the next plan -- which on a machine that had lost contact was never.
    #
    # A window and not an equality, because the session budget is being spent
    # while this runs and the only direction that drift goes is down.
    def left_here(box):
        for budget in json_cli(box, 'status', child)['budgets']:
            if budget['id'] == 'session':
                return budget['leftSeconds']
        raise vm.Failed('no session budget to read on ' + box.machine)

    before_here = left_here(dad)
    handed = json_cli(dad, 'grant', child, '--session', '10m')
    if not before_here + 600 - 30 <= handed['leftSeconds'] <= before_here + 600:
        raise vm.Failed(f'a 10m grant left {handed["leftSeconds"]}s on the machine it was '
                        f'typed on, from {before_here}s: the minutes did not land here')
    print(f'      the grant was live on the spot: {before_here}s became '
          f'{handed["leftSeconds"]}s')
    deadline = time.monotonic() + 150
    while time.monotonic() < deadline:
        updated = json_cli(dad, 'allocation', 'show', child)['reservation']
        if updated['revision'] > initial['revision']:
            after = {d['house']['session']['credit'] for d in updated['documents'].values()}
            if after != {7800}:
                raise vm.Failed(f'a 10m grant made the credits {after}, expected 7800 on both')
            peer = allocation_on(vm, child, json_cli)
            if peer['revision'] == updated['revision']:
                break
        time.sleep(2)
    else:
        raise vm.Failed('reconnected peer never received the extra credit')
    print('      restart, a silent machine and reconnection passed; exactly 600s added')
    spending_moves_the_other_balance(vm, dad, child, json_cli)


def _spend_more(child, seconds):
    """Another `seconds` on the far machine's session, and nothing else touched.

    Written against the file rather than through a verb because the daemon that
    would earn it is stopped: `seed_ledger` in the reference setup stops it to
    write a deterministic day, and it never comes back. The first run of this
    case assumed otherwise, waited three minutes for a number that could not
    move, and said so.

    `seed_ledger` itself is no good here either -- it writes a whole ledger, and
    the `allocation` and `observedAt` inside this one are what the manager's
    plan checks before it will issue anything. So this reads, adds and writes
    back, and leaves every other field where it was.

    What that means for what this case proves: the *accrual* of time is
    `close_takes_the_scope_not_the_session`, which seeds a ledger, starts the
    real daemon and waits for a budget to run out against the real clock -- an
    ending that only arrives if the seconds were counted. What is measured here
    is the other half: the arithmetic and the trip, a number that grew on one
    computer arriving in the other's statement.
    """
    import shlex
    return shlex.quote(
        "import json, glob\n"
        f"for p in glob.glob('/var/lib/omahouse/{child}/*.json'):\n"
        "    d=json.load(open(p))\n"
        f"    d['budgets']['session'] = d['budgets'].get('session', 0) + {int(seconds)}\n"
        "    open(p,'w').write(json.dumps(d, indent=2))\n")


def spending_moves_the_other_balance(vm, dad, child, json_cli):
    """The one thing the portions could not do, and the reason they went.

    Under a portion, minutes reserved on the machine in the bedroom were minutes
    the one in the kitchen could not spend, however idle the first one was. Under
    a balance both are told the same credit and what the *other* has spent of it,
    so an hour is an hour wherever the person sits.

    The trip is what is measured: a number that grew on `poc` has to arrive in
    the manager's own statement, carried by a scheduled cycle and nobody's hand.
    """
    def statement(box):
        return allocation_on(box, child, json_cli)['house']['session']

    def spent_here(box):
        for budget in json_cli(box, 'status', child)['budgets']:
            if budget['id'] == 'session':
                return budget['usedSeconds']
        raise vm.Failed('no session budget to read on ' + box.machine)

    before = statement(dad)
    mine = spent_here(dad)
    print(f'      the manager is told {before["elsewhere"]}s spent elsewhere, '
          f'{mine}s of its own')

    added = 300
    vm.root('python3 -c ' + _spend_more(child, added))
    if spent_here(vm) != before['elsewhere'] + added:
        raise vm.Failed('the far machine did not take the extra time: '
                        f'{spent_here(vm)}s, expected {before["elsewhere"] + added}s')

    began = time.monotonic()
    deadline = began + 240
    while time.monotonic() < deadline:
        now = statement(dad)
        if now['elsewhere'] > before['elsewhere']:
            # It has to be the other computer's spending and not this one's, and
            # not a grant: either would move a number here and neither would be
            # the thing this case is about.
            if spent_here(dad) != mine:
                raise vm.Failed('the manager spent time of its own during the check, '
                                'so the movement below proves nothing')
            if now['credit'] != before['credit']:
                raise vm.Failed(f'the credit moved too ({before["credit"]} to '
                                f'{now["credit"]}), so this is a grant and not spending')
            moved = now['elsewhere'] - before['elsewhere']
            if moved != added:
                raise vm.Failed(f'{added}s were spent on the far machine and {moved}s '
                                'arrived')
            print(f'      {added}s spent on {vm.machine} reached {dad.machine} in '
                  f'{time.monotonic() - began:.0f}s, without anybody dividing anything')
            return
        time.sleep(2)
    raise vm.Failed('time spent on one computer never reached the other machine: '
                    f'still {statement(dad)["elsewhere"]}s elsewhere after 240s')
