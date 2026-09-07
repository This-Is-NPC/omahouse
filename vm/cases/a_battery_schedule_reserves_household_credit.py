"""A real node scheduler collects, plans and applies without a manual sync run.

Two disposable guests, the actual Battery and HTTP API. Repeated fires and a
manager restart cannot refill the portions; an offline peer keeps its share.
"""
import importlib.util
import json
import shlex
import time
from pathlib import Path

MACHINE = 'poc'
WHY = 'the Omakure scheduler runs the Battery and preserves exclusive daily credit'


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
        raise vm.Failed(f'expected two exclusive portions: {initial}')
    total = sum(d['limits']['session'] for d in initial['documents'].values())
    if total != 7200:
        raise vm.Failed(f'portions total {total}, expected 7200')
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
        raise vm.Failed('an offline peer released its reserved portion')
    vm.root('systemctl start omakure-node.service')
    json_cli(dad, 'grant', child, '--session', '10m')
    deadline = time.monotonic() + 150
    while time.monotonic() < deadline:
        updated = json_cli(dad, 'allocation', 'show', child)['reservation']
        if updated['revision'] > initial['revision']:
            allocated = sum(d['limits']['session'] for d in updated['documents'].values())
            if allocated != 7800:
                raise vm.Failed(f'a 10m grant became {allocated} total seconds')
            peer = json_cli(vm, 'profile', 'show', child)['allocation']
            if peer['revision'] == updated['revision']:
                break
        time.sleep(2)
    else:
        raise vm.Failed('reconnected peer never received the extra credit')
    print('      restart, offline reservation and reconnection passed; exactly 600s added')
