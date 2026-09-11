"""A central draft reaches a chosen computer through the installed Battery.

The receiving machine starts with no profile. Publication reads it over the
console API, delivers the central document, and records the result. A later
client edit must block publication until a person resolves the observed copy;
both taking its version and keeping the central version are exercised. An
unreachable node must leave the last publication record untouched.
"""
import json
import shlex

MACHINE = 'poc'
WHY = 'central drafts publish over the Battery and client edits require a decision'

WORKSPACE = '/var/lib/omakure-workspace'
THEIRS = 'the kitchen laptop'
WIRE = 7879


def cli(box, *args, check=True):
    return box.root('omahouse ' + shlex.join(args), check=check)


def document(box, user):
    return json.loads(cli(box, 'profile', 'show', user, '--json'))


def run(vm):
    dad = vm.peer('dad')
    dad.put(str(vm.build_binary), '/tmp/omahouse')
    dad.root('install -Dm755 /tmp/omahouse /usr/bin/omahouse')
    try:
        for box in (vm, dad):
            box.fresh_omakure()
        invited = json.loads(cli(dad, '--json', 'machine', 'invite',
                                 '--at', f'{dad.address}:{WIRE}', '--name', 'the study'))
        prepared = json.loads(cli(vm, '--json', 'machine', 'prepare',
                                  '--invite', invited['invite'],
                                  '--at', f'{vm.address}:{WIRE}', '--name', THEIRS))
        added = json.loads(cli(dad, '--json', 'machine', 'add', THEIRS,
                               '--pair', prepared['pair']))
        if not added.get('trusted'):
            raise vm.Failed('pairing did not finish')
        # Pairing installed every runtime adapter from the public Battery.
        exercise(vm, dad)
    finally:
        dad.root('rm -f /usr/bin/omahouse', check=False)


def exercise(vm, dad):
    Failed = vm.Failed
    user = vm.subject
    published_path = '/var/lib/omahouse/elsewhere/' + THEIRS + '/' + user + '/published.json'

    def publish(expected):
        code, output = cli(dad, 'profile', 'publish', user, '--to', THEIRS, '--json', check=False)
        if code != expected:
            raise Failed(f'profile publish should exit {expected}, got {code}: {output[:900]}')
        try:
            return json.loads(output)
        except ValueError as error:
            raise Failed(f'publication did not produce its JSON report: {output[:900]}') from error

    def report_row(report):
        rows = [row for row in report['machines'] if row['machine'] == THEIRS]
        if len(rows) != 1:
            raise Failed(f'the target has no unique publication row: {report}')
        return rows[0]

    def publication_record():
        return dad.root('cat ' + shlex.quote(published_path))

    def same_rules(left, right):
        return {key: value for key, value in left.items() if key not in ('writtenAt', 'writtenBy', 'allocation')} == {
            key: value for key, value in right.items() if key not in ('writtenAt', 'writtenBy', 'allocation')}

    def assert_up_to_date():
        profiles = json.loads(cli(dad, 'profile', 'list', '--json'))
        profile = next(row for row in profiles if row['user'] == user)
        if profile['publication'] != 'up to date' or profile['unresolved']:
            raise Failed(f'the publication record does not stand current: {profile}')

    # Enrollment enables enforcement and removes grace. Start with those
    # policies using the shared fixture (grace has no CLI verb), so enrolling
    # later changes runtime state only and cannot create a policy conflict.
    dad.make_profile({'session': 120}, grace=0)
    if json.loads(cli(vm, 'profile', 'list', '--json')):
        raise Failed('the receiving machine must start without a profile')
    draft = document(dad, user)['profiles'][0]
    first = publish(0)
    if 'published' not in report_row(first)['said']:
        raise Failed(f'the first delivery was not reported: {first}')
    installed = document(vm, user)['profiles'][0]
    if not same_rules(draft, installed) or installed.get('writtenBy') != 'omakure':
        raise Failed(f'the draft did not arrive through the node account: {installed}')
    record = json.loads(publication_record())
    if record['schemaVersion'] != 1 or not same_rules(record['profile'], draft):
        raise Failed(f'the recorded publication is not the central draft: {record}')
    assert_up_to_date()
    text = cli(dad, 'profile', 'publish', user, '--to', THEIRS)
    if THEIRS not in text or 'up to date' not in text:
        raise Failed(f'the operator-facing publication table lost its target or state: {text}')
    for line in text.strip().splitlines():
        print('      ' + line)
    print('      a central-only draft arrived through the Battery; the list says up to date')

    cli(vm, 'limit', user, '--session', '1h')
    changed = document(vm, user)
    before = publication_record()
    blocked = publish(1)
    if not blocked['unresolved'] or THEIRS not in blocked['changedOn']:
        raise Failed(f'the client edit did not block publication: {blocked}')
    if document(vm, user) != changed or publication_record() != before:
        raise Failed('a blocked publication changed the client or its publication record')
    cli(dad, 'profile', 'merge', user, '--take', THEIRS)
    publish(0)
    if not same_rules(document(dad, user)['profiles'][0], changed['profiles'][0]):
        raise Failed('taking the client version did not retain its rules centrally')
    assert_up_to_date()
    print('      a client edit blocked publication until merge --take resolved it')

    cli(vm, 'limit', user, '--session', '30m')
    changed = document(vm, user)
    blocked = publish(1)
    if not blocked['unresolved']:
        raise Failed('a second client edit did not need a fresh decision')
    kept = json.loads(cli(dad, 'profile', 'merge', user, '--keep', THEIRS))
    if kept.get('supersedes') != changed['profiles'][0]:
        raise Failed('merge --keep lost the whole observed copy in its document')
    publish(0)
    if not same_rules(document(vm, user)['profiles'][0], document(dad, user)['profiles'][0]):
        raise Failed('the durable keep decision did not allow the central draft to land')
    assert_up_to_date()
    print('      merge --keep resolved the observed copy and publication restored central rules')

    # Enrollment attaches different runtime allocations to otherwise identical
    # policies: the central is 'here', the client is its fleet name. Publishing
    # rules must neither mistake those bindings for edits nor transplant them.
    def synchronize(action):
        result = json.loads(dad.root('-u omakure env HOME=' + WORKSPACE
            + ' OMAKURE_SCRIPTS_DIR=' + WORKSPACE + ' python3 ' + WORKSPACE
            + '/omahouse-sync.py --action ' + action + ' --users ' + shlex.quote(user)))
        if not result.get('ok'):
            raise Failed(f'the enrolled household did not synchronize: {result}')

    before_enrollment = {box.machine: document(box, user)['profiles'][0] for box in (dad, vm)}
    synchronize('enroll')
    for box in (dad, vm):
        if not same_rules(before_enrollment[box.machine], document(box, user)['profiles'][0]):
            raise Failed(f'the fixture allowed enrollment to change {box.machine} policy')
    allocations = {box.machine: document(box, user)['profiles'][0]['allocation']
                   for box in (dad, vm)}
    if allocations[dad.machine]['machine'] != 'here' or allocations[vm.machine]['machine'] != THEIRS:
        raise Failed('enrollment did not bind each computer to its own allocation')
    cli(dad, 'limit', user, '--session', '90m')
    enrolled = publish(0)
    if 'published' not in report_row(enrolled)['said']:
        raise Failed(f'the enrolled policy update was not delivered: {enrolled}')
    for box in (dad, vm):
        current = document(box, user)['profiles'][0]['allocation']
        if current != allocations[box.machine]:
            raise Failed(f'publication changed {box.machine} runtime allocation: {current}')
    assert_up_to_date()
    synchronize('sync')
    if document(dad, user)['profiles'][0]['allocation']['machine'] != 'here' \
            or document(vm, user)['profiles'][0]['allocation']['machine'] != THEIRS:
        raise Failed('the next synchronization lost a machine binding')
    assert_up_to_date()
    print('      enrolled rules republished without changing allocations; the next cycle succeeded')

    cli(dad, 'limit', user, '--session', '2h')
    before = publication_record()
    vm.root('systemctl stop omakure-node.service')
    try:
        offline = publish(1)
        if THEIRS not in json.dumps(offline['notChecked']):
            raise Failed(f'the offline machine was not named as not checked: {offline}')
        if 'unreachable' not in report_row(offline)['said']:
            raise Failed(f'the target was not reported unreachable: {offline}')
        if publication_record() != before:
            raise Failed('an unreachable target advanced its publication record')
    finally:
        vm.root('systemctl start omakure-node.service')
    print('      a stopped node is not checked and unreachable; its last publication stands')
