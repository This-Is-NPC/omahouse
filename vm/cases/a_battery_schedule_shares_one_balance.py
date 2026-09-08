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
    original_remote = json_cli(vm, 'profile', 'show', child)['allocation']
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
    print(f'      {len(first)} actual scheduled runs completed; portions still total 7200s')
    dad.root('systemctl restart omakure-node.service')
    wait_rows(3, True)
    if json_cli(vm, 'profile', 'show', child)['allocation'] != original_remote:
        raise vm.Failed('manager restart recharged the remote machine')
    vm.root('systemctl stop omakure-node.service')
    wait_rows(1, False)
    if json_cli(dad, 'allocation', 'show', child)['reservation'] != initial:
        raise vm.Failed('a machine that stopped reporting changed the household balance')
    vm.root('systemctl start omakure-node.service')
    json_cli(dad, 'grant', child, '--session', '10m')
    deadline = time.monotonic() + 150
    while time.monotonic() < deadline:
        updated = json_cli(dad, 'allocation', 'show', child)['reservation']
        if updated['revision'] > initial['revision']:
            after = {d['house']['session']['credit'] for d in updated['documents'].values()}
            if after != {7800}:
                raise vm.Failed(f'a 10m grant made the credits {after}, expected 7800 on both')
            peer = json_cli(vm, 'profile', 'show', child)['allocation']
            if peer['revision'] == updated['revision']:
                break
        time.sleep(2)
    else:
        raise vm.Failed('reconnected peer never received the extra credit')
    print('      restart, a silent machine and reconnection passed; exactly 600s added')
    spending_moves_the_other_balance(vm, dad, child, json_cli)


def spending_moves_the_other_balance(vm, dad, child, json_cli):
    """The one thing the portions could not do, and the reason they went.

    Under a portion, minutes reserved on the machine in the bedroom were minutes
    the one in the kitchen could not spend, however idle the first one was. Under
    a balance both are told the same credit and what the *other* has spent of it,
    so an hour is an hour wherever the person sits.

    Nothing is simulated here. `poc` has the child's session open and its own
    `omahouse watch` is debiting it every couple of seconds, so the spending is
    real; what is being waited for is that spending reaching the *manager's*
    statement, which is the trip a portion never made.
    """
    def statement(box):
        return json_cli(box, 'profile', 'show', child)['allocation']['house']['session']

    def spent_here(box):
        for budget in json_cli(box, 'status', child)['budgets']:
            if budget['id'] == 'session':
                return budget['usedSeconds']
        raise vm.Failed('no session budget to read on ' + box.machine)

    before = statement(dad)
    mine = spent_here(dad)
    print(f'      the manager is told {before["elsewhere"]}s spent elsewhere, '
          f'{mine}s of its own')

    deadline = time.monotonic() + 180
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
            print(f'      and {moved}s later, without anybody dividing anything')
            return
        time.sleep(2)
    raise vm.Failed('time spent on one computer never reached the other machine: '
                    f'still {statement(dad)["elsewhere"]}s elsewhere after 180s')
