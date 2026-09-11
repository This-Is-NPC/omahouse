#!/usr/bin/env bash
# Build `omahouse-dad`, the operator's own machine, from the poc.
#
# The two-machine case needs a computer that is *not* fiscalised: no omahouse,
# no daemon, no /etc/omahouse. It conducts and it reads, and that asymmetry is
# the thing being tested -- a console that needed omahouse installed beside it
# would make this two managed machines rather than an operator and a subject.
#
# This exists because the first one was built by hand and could not be rebuilt
# when it stopped taking an address. A machine nobody can recreate is a machine
# that has to be nursed.
#
# Two things a clone of an Arch image needs, and neither is obvious:
#
#   - `10-cloud-init-eth0.network` matches on the MAC the image was born with,
#     so a clone matches nothing and comes up with no network at all. The poc
#     carries an inert `20-any-ethernet.network` that matches by name instead,
#     and the clone inherits it.
#   - a clone that keeps its machine-id keeps its DHCP identity, so both
#     machines are handed the same address. Measured: the case's own guard
#     caught it and refused rather than testing a machine against itself.
set -euo pipefail

uri=${OMAHOUSE_VM_URI:-qemu:///system}
source_domain=${1:-omahouse-poc}
domain=${2:-omahouse-dad}
disk="/var/lib/libvirt/images/${domain}.qcow2"
key=${OMAHOUSE_VM_KEY:-$HOME/.ssh/id_vms}
operator=${OMAHOUSE_VM_OPERATOR:-arch}

say() { printf '  %s\n' "$*"; }
virsh_() { virsh -c "$uri" "$@"; }

[[ $(virsh_ domstate "$source_domain" 2>/dev/null) == "shut off" ]] \
    || { say "$source_domain must be shut off to clone it"; exit 1; }

if virsh_ dominfo "$domain" >/dev/null 2>&1; then
    say "removing the existing $domain"
    virsh_ destroy "$domain" >/dev/null 2>&1 || true
    virsh_ undefine "$domain" --nvram >/dev/null 2>&1 || virsh_ undefine "$domain" >/dev/null
    virsh_ vol-delete --pool images "${domain}.qcow2" >/dev/null 2>&1 || true
fi

say "cloning $source_domain"
virt-clone --connect "$uri" --original "$source_domain" --name "$domain" --file "$disk" >/dev/null

say "booting it"
virsh_ start "$domain" >/dev/null
address=""
for _ in $(seq 1 60); do
    address=$(virsh_ domifaddr "$domain" 2>/dev/null \
        | grep -oE '192\.168\.[0-9]+\.[0-9]+' | head -1) && [[ -n $address ]] && break
    sleep 5
done
[[ -n $address ]] || { say "$domain never took an address; is 20-any-ethernet.network on $source_domain?"; exit 1; }
say "it is at $address"

ssh_() {
    ssh -i "$key" -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
        -o LogLevel=ERROR -o ConnectTimeout=10 -o BatchMode=yes \
        "${operator}@${address}" "$@"
}
for _ in $(seq 1 30); do ssh_ true 2>/dev/null && break; sleep 5; done

say "giving it an identity of its own"
ssh_ "sudo hostnamectl set-hostname ${domain}
      sudo rm -f /etc/machine-id /var/lib/dbus/machine-id
      sudo systemd-machine-id-setup >/dev/null 2>&1
      sudo ln -sf /etc/machine-id /var/lib/dbus/machine-id
      sudo rm -f /etc/ssh/ssh_host_*; sudo ssh-keygen -A >/dev/null 2>&1"

say "taking omahouse off it, because the operator's machine is not under rules"
ssh_ "sudo systemctl disable --now omahouse.service >/dev/null 2>&1 || true
      sudo rm -f /usr/bin/omahouse /usr/local/bin/omahouse /usr/bin/omahouse-studio
      sudo rm -f /usr/lib/systemd/system/omahouse.service
      sudo rm -rf /etc/omahouse /var/lib/omahouse"

say "rebooting onto the new identity"
ssh_ "sudo systemctl reboot" || true
sleep 10
address=""
for _ in $(seq 1 60); do
    address=$(virsh_ domifaddr "$domain" 2>/dev/null \
        | grep -oE '192\.168\.[0-9]+\.[0-9]+' | head -1) && [[ -n $address ]] && break
    sleep 5
done
[[ -n $address ]] || { say "$domain did not come back with an address"; exit 1; }

for _ in $(seq 1 30); do ssh_ true 2>/dev/null && break; sleep 5; done
say "it says it is $(ssh_ 'uname -n'), omahouse is $(ssh_ 'command -v omahouse || echo absent'), at $address"
virsh_ shutdown "$domain" >/dev/null
say "shut down; the case will start it when it needs it"
