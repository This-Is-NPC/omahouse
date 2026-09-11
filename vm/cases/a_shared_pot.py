"""Two real machines spend one pot, asked for with a verb and shared by the scheduler.

The case `docs/design.md` §3 exists for, and the
one nothing could run until a verb could write `resets`: a credit that belongs
to the person, spent wherever they sit, with the turn of the date giving none
of it back.

What has to hold, in order:

  1. the pot is asked for on the manager with `omahouse limit --resets never`
     and reaches the machine under rules the way every profile does -- by a
     push over the console API, through the stage, superseding the copy the
     manager collected -- and a merge afterwards calls the two copies `same`
     under two different stamps, because it reads the rules and not the stamp;
  2. once enrolled, both machines are told the same credit, which is the pot
     itself, and read the same balance;
  3. seconds spent on one machine come off the balance the other reads,
     carried by a scheduled cycle and nobody's hand, and the credit does not
     move -- the trip is what is measured;
  4. the date turning on the spending machine refills nothing and loses
     nothing: the pot's running total walks into the new day, the document the
     house collects still carries it, and the next scheduled cycle leaves the
     household's numbers exactly where they were.

The daemon on the machine under rules is stopped for the whole case, so every
second here is the case's own and the arithmetic is exact. Accrual against the
real clock is `close_takes_the_scope_not_the_session`; what is measured here is
the trip and the carry.
"""
import importlib.util
import json
import shlex
import time
from pathlib import Path

MACHINE = 'poc'
WHY = 'one pot, asked for with a verb, spent from two machines through the scheduler'

WORKSPACE = '/var/lib/omakure-workspace'
# What the reference setup calls the machine under rules in the household.
THEIRS = 'the kitchen laptop'
POT_SECONDS = 2 * 3600
SPENT = 300


def run(vm):
    dad = vm.peer('dad')
    # Pairing, the Battery from this disk, a profile on both and a collected
    # day: the setup `a_battery_schedule_shares_one_balance` reuses, reused
    # here for the same reason -- it is asserted on its own and this case is
    # about what happens after it.
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


def omk(box, *args):
    return box.root('-u omakure env HOME=' + WORKSPACE + ' OMAKURE_SCRIPTS_DIR=' + WORKSPACE
                    + ' omakure ' + ' '.join(shlex.quote(a) for a in args))


def node(box, script, *args):
    """A Battery script, run as the node account with the workspace it would have."""
    return box.root('-u omakure env HOME=' + WORKSPACE + ' OMAKURE_SCRIPTS_DIR=' + WORKSPACE
                    + ' python3 ' + WORKSPACE + '/' + script + ' '
                    + ' '.join(shlex.quote(a) for a in args))


def json_cli(box, *args):
    return json.loads(box.root('omahouse --json ' + ' '.join(shlex.quote(a) for a in args)))


def cue(dad, machine, script, args):
    """Run a Battery script on `machine` from the manager, over the console API.

    The same road `omahouse-sync.py` takes for a day and a statement, written
    out here rather than imported so that the push travels exactly the way the
    README says it does: the manager reads the machine's credential with
    `machine token`, starts a run, and waits for its exit code.
    """
    code = (
        "import json, subprocess, sys, time, urllib.request\n"
        f"key = subprocess.run(['omahouse', 'machine', 'token', {machine!r}],"
        " capture_output=True, text=True, check=True).stdout.strip().splitlines()\n"
        "endpoint, token = key\n"
        "def request(path, body=None):\n"
        "    data = json.dumps(body).encode() if body is not None else None\n"
        "    req = urllib.request.Request('http://' + endpoint + path, data=data,"
        " headers={'Authorization': 'Bearer ' + token, 'Content-Type': 'application/json'})\n"
        "    with urllib.request.urlopen(req, timeout=10) as response:\n"
        "        result = json.load(response)\n"
        "    if not result.get('ok'):\n"
        "        sys.exit('the API refused: ' + json.dumps(result))\n"
        "    return result['data']\n"
        f"started = request('/v1/runs', {{'script': {script!r}, 'args': {args!r}}})\n"
        "deadline = time.monotonic() + 60\n"
        "while time.monotonic() < deadline:\n"
        "    row = request('/v1/runs/' + str(started['run_id']))\n"
        "    if row['state'] in ('queued', 'running'):\n"
        "        time.sleep(0.25)\n"
        "        continue\n"
        "    print(json.dumps(row))\n"
        "    sys.exit(0 if row.get('exit_code') == 0 else 1)\n"
        "sys.exit('the run never finished')\n")
    return dad.root('python3 -c ' + shlex.quote(code))


def _spend_pot(child, seconds):
    """Another `seconds` on the pot, in the counter a pot really spends.

    Written against the file because the daemon is stopped, as
    `a_battery_schedule_shares_one_balance` does for the daily counter. A pot
    spends `kept` and never `budgets`: that is the whole of what `resets: never`
    is, and it is what this case is about.
    """
    return shlex.quote(
        "import json, glob\n"
        f"for p in glob.glob('/var/lib/omahouse/{child}/*.json'):\n"
        "    d = json.load(open(p))\n"
        "    kept = d.setdefault('kept', {})\n"
        f"    kept['session'] = kept.get('session', 0) + {int(seconds)}\n"
        "    open(p, 'w').write(json.dumps(d, indent=2))\n")


def _turn_the_date(child):
    """Today's ledger becomes yesterday's, and today has no file.

    The cheapest honest turn of the date: the machine's clock is not moved,
    because the manager's freshness guard and the scheduler are on the same
    clock and the case would be measuring its own lie. What a real turn leaves
    behind is exactly this -- a last day whose name is not today, and nothing
    for today -- and `readDay` is what meets it.
    """
    return shlex.quote(
        "import json, glob, os, datetime\n"
        f"paths = sorted(glob.glob('/var/lib/omahouse/{child}/*.json'))\n"
        "today = paths[-1]\n"
        "d = json.load(open(today))\n"
        "yesterday = (datetime.date.fromisoformat(d['date'])"
        " - datetime.timedelta(days=1)).isoformat()\n"
        "d['date'] = yesterday\n"
        f"open('/var/lib/omahouse/{child}/' + yesterday + '.json', 'w')"
        ".write(json.dumps(d, indent=2))\n"
        "os.remove(today)\n"
        "print(yesterday)\n")


def exercise(vm, dad):
    Failed = vm.Failed
    child = vm.subject

    # Every second below is the case's own.
    vm.stop_daemon()
    # The reference setup seeds half an hour on the daily counter, which a pot
    # never reads. A day with nothing on it, so that the only counter that moves
    # is the one this case writes to.
    vm.seed_ledger({})

    def statement(box):
        profiles = json_cli(box, 'profile', 'show', child)['profiles']
        if len(profiles) != 1:
            raise Failed(f'{box.machine} shows {len(profiles)} profiles for {child}')
        return profiles[0]['allocation']['house']['session']

    def balance(box):
        for budget in json_cli(box, 'status', child)['budgets']:
            if budget['id'] == 'session':
                return budget
        raise Failed('no session budget to read on ' + box.machine)

    # -- 1. asked for on the manager, pushed to the machine --------------------
    said = dad.root(f'omahouse limit {child} --session 2h --resets never')
    if 'never resets' not in said:
        raise Failed(f'the verb did not say what it wrote: {said!r}')
    theirs = vm.root(f'omahouse profile show {child} --json')
    dad.root(f'omahouse profile collect {shlex.quote(THEIRS)} {child}', stdin=theirs)
    kinds = {row['machine']: row['is']
             for row in json_cli(dad, 'profile', 'merge', child)['machines']}
    if kinds.get(THEIRS) != 'changed':
        raise Failed(f'the manager wrote a pot and the merge says {kinds}')
    document = dad.root(f'omahouse profile merge {child} --keep {shlex.quote(THEIRS)}')
    omk(vm, 'battery', 'install', 'omahouse', 'omahouse.profile-push', '--force')
    vm.root('systemctl restart omakure-node.service')
    cue(dad, THEIRS, 'omahouse-profile-push.py', ['--document', document.strip()])

    mine = json_cli(vm, 'profile', 'show', child)['profiles'][0]
    session = [b for b in mine['budgets'] if b['id'] == 'session']
    if len(session) != 1 or session[0].get('resets') != 'never' \
            or session[0].get('dailyMinutes') != POT_SECONDS // 60:
        raise Failed(f'the pushed pot did not land as written: {mine["budgets"]}')
    if mine.get('writtenBy') != 'omakure':
        raise Failed(f'a push has no person behind it, and this one says {mine.get("writtenBy")!r}')
    # Two copies with the same rules and two different stamps: the manager's own
    # and the node's. `same`, because the merge reads the rules; read the stamp
    # it would list this machine as changed forever.
    dad.root(f'omahouse profile collect {shlex.quote(THEIRS)} {child}',
             stdin=vm.root(f'omahouse profile show {child} --json'))
    kinds = {row['machine']: row['is']
             for row in json_cli(dad, 'profile', 'merge', child)['machines']}
    if kinds.get(THEIRS) != 'same':
        raise Failed(f'the same rules under another stamp read as {kinds}')
    print('      the pot was asked for on the manager and pushed; the merge calls the two '
          'copies the same under different stamps')

    # -- 2. enrolled, and told the same credit -------------------------------
    for box in (vm, dad):
        for script in ('omahouse.allocation', 'omahouse.sync'):
            omk(box, 'battery', 'install', 'omahouse', script, '--force')
    vm.root('systemctl restart omakure-node.service')
    enrolled = json.loads(node(dad, 'omahouse-sync.py', '--action', 'enroll', '--users', child))
    if not enrolled['ok']:
        raise Failed(f'enrollment did not finish: {enrolled}')
    initial = json_cli(dad, 'allocation', 'show', child)['reservation']
    credits = {d['house']['session']['credit'] for d in initial['documents'].values()}
    if credits != {POT_SECONDS}:
        raise Failed(f'the pot is {POT_SECONDS}s and the machines were told {credits}')
    for box in (vm, dad):
        here = balance(box)
        if here['resets'] != 'never' or here['leftSeconds'] != POT_SECONDS:
            raise Failed(f'{box.machine} reads {here} before anything was spent')
    print(f'      both machines are told the same {POT_SECONDS}s credit and read the same '
          'balance')

    dad.put(str(dad.battery_checkout / '.scripts/configure-sync.py'), '/tmp/configure-sync.py')
    dad.root('-u omakure python3 /tmp/configure-sync.py --workspace ' + WORKSPACE
             + ' --users ' + shlex.quote(child) + ' --cron ' + shlex.quote('*/10 * * * * *'))
    dad.root('systemctl restart omakure-node.service')

    def scheduled():
        data = json.loads(omk(dad, '--json', 'history', 'list', '--limit', '100'))['data']
        rows = data.get('runs', data.get('items', [])) if isinstance(data, dict) else data
        return [r for r in rows if r.get('script_path', '').endswith('omahouse-schedule.py')
                and r.get('trigger') in ('Scheduled', 'scheduled')
                and r.get('state') in ('completed', 'failed')]

    def wait_for_cycles(count, patience=150):
        """Until `count` scheduled cycles have finished, none of them red."""
        deadline = time.monotonic() + patience
        while time.monotonic() < deadline:
            rows = scheduled()
            failed = [r for r in rows if r.get('exit_code') != 0]
            if failed:
                raise Failed(f'a scheduled cycle went red: {failed[-1]}')
            if len(rows) >= count:
                return rows
            time.sleep(2)
        raise Failed(f'the scheduler did not finish {count} cycles: {scheduled()}')

    wait_for_cycles(1)

    # -- 3. spent here, read there -------------------------------------------
    before = statement(dad)
    if before['elsewhere'] != 0 or before['credit'] != POT_SECONDS:
        raise Failed(f'the manager starts from {before}, not from an untouched pot')
    vm.root('python3 -c ' + _spend_pot(child, SPENT))
    spent_here = balance(vm)
    if spent_here['usedSeconds'] != SPENT or spent_here['leftSeconds'] != POT_SECONDS - SPENT:
        raise Failed(f'the spending machine reads {spent_here} after {SPENT}s')

    began = time.monotonic()
    deadline = began + 120
    while time.monotonic() < deadline:
        now = statement(dad)
        if now['elsewhere'] > 0:
            trip = time.monotonic() - began
            if now['elsewhere'] != SPENT:
                raise Failed(f'{SPENT}s were spent on {vm.machine} and {now["elsewhere"]}s '
                             'arrived')
            if now['credit'] != POT_SECONDS:
                raise Failed(f'the credit moved ({POT_SECONDS} to {now["credit"]}), so this '
                             'is a refill and not spending')
            there = balance(dad)
            if there['leftSeconds'] != POT_SECONDS - SPENT or there['usedSeconds'] != 0:
                raise Failed(f'the other machine reads {there}, not {POT_SECONDS - SPENT}s left')
            break
        time.sleep(2)
    else:
        raise Failed(f'{SPENT}s spent on one machine never reached the other: still '
                     f'{statement(dad)["elsewhere"]}s elsewhere after 120s')
    print(f'      {SPENT}s spent on {vm.machine} came off {dad.machine}\'s balance in '
          f'{trip:.0f}s: {POT_SECONDS}s left became {POT_SECONDS - SPENT}s, with the credit '
          'unchanged')

    # -- 4. the date turns where the time was spent --------------------------
    cycles_before = len(scheduled())
    yesterday = vm.root('python3 -c ' + _turn_the_date(child)).strip()
    morning = balance(vm)
    if morning['usedSeconds'] != SPENT or morning['leftSeconds'] != POT_SECONDS - SPENT \
            or morning['resets'] != 'never':
        raise Failed(f'the morning after {yesterday}, the spending machine reads {morning}')
    # The document the house collects, on that same morning, before anything
    # has written today: it has to carry the pot or the manager is told it was
    # never touched.
    told = json_cli(vm, 'day', child)
    if told.get('kept', {}).get('session') != SPENT:
        raise Failed(f'the day this machine reports the morning after carries {told.get("kept")}')
    wait_for_cycles(cycles_before + 1)
    after = statement(dad)
    if after != before | {'elsewhere': SPENT}:
        raise Failed(f'the turn of the date moved the household from '
                     f'{before | {"elsewhere": SPENT}} to {after}')
    there = balance(dad)
    if there['leftSeconds'] != POT_SECONDS - SPENT:
        raise Failed(f'after the turn the other machine reads {there}')
    print(f'      the date turned on {vm.machine} and a scheduled cycle later the house '
          f'still reads {POT_SECONDS - SPENT}s left: nothing refilled, nothing lost')
