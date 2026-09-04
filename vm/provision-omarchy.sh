#!/usr/bin/env bash
# Build `omahouse-omarchy`: a libvirt machine running real Omarchy, with
# omahouse installed from this working tree and a profile already on it.
#
# This is the machine somebody opens and uses by hand. It is not the test rig --
# `vm/run.sh` drives `omahouse-poc`, which this script never touches -- and the
# two are kept apart on purpose: one is disposable and asserts, the other is
# looked at.
#
# The Omarchy ISO is an interactive TUI and cannot be automated. What can be
# automated is the repository the ISO installs from:
#
#     [omarchy]
#     Server = https://pkgs.omarchy.org/stable/$arch
#
# So this pacstraps the same packages onto an Arch cloud image and then runs the
# subset of /usr/share/omarchy/install that is not about a bootloader or a
# printer. What comes up is a real Omarchy session: hyprland under uwsm, the
# quickshell bar, SDDM with the Omarchy greeter, and the cgroup layout omahouse
# is written against.
#
#     vm/provision-omarchy.sh              build it, or bring it up to date
#     vm/provision-omarchy.sh --recreate   throw the disk away and start over
#
# What it needs on the host: libvirt reachable without sudo (be in `libvirt`),
# `~/.ssh/id_vms`, and the `omahouse-arch-base.qcow2` volume in the `images`
# pool. It installs nothing on the host.
set -euo pipefail

DOMAIN=omahouse-omarchy
POOL=images
BASE_VOL=omahouse-arch-base.qcow2
DISK_VOL=$DOMAIN.qcow2
SEED_VOL=$DOMAIN-seed.iso
ADDRESS=192.168.122.60
MAC=52:54:00:a2:56:7b
KEY=$HOME/.ssh/id_vms
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

export LIBVIRT_DEFAULT_URI=qemu:///system

say() { printf '\n== %s\n' "$*"; }
guest() {
    ssh -i "$KEY" -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
        -o LogLevel=ERROR -o ConnectTimeout=10 -o BatchMode=yes \
        "howl@$ADDRESS" "$@"
}

recreate=0
[ "${1:-}" = "--recreate" ] && recreate=1

# -- 1. the disk, the seed and the domain -------------------------------------

if [ "$recreate" = 1 ]; then
    say "throwing away the old $DOMAIN"
    virsh destroy "$DOMAIN" >/dev/null 2>&1 || true
    virsh undefine "$DOMAIN" --nvram >/dev/null 2>&1 || true
    virsh vol-delete --pool "$POOL" "$DISK_VOL" >/dev/null 2>&1 || true
    virsh vol-delete --pool "$POOL" "$SEED_VOL" >/dev/null 2>&1 || true
fi

if ! virsh vol-info --pool "$POOL" "$DISK_VOL" >/dev/null 2>&1; then
    say "overlay on $BASE_VOL"
    # 40G because Omarchy, Chromium, Qt and a `makepkg` of this tree are about
    # nine of them and a demo machine should not run out while somebody uses it.
    virsh vol-create-as "$POOL" "$DISK_VOL" 40G --format qcow2 \
        --backing-vol "$BASE_VOL" --backing-vol-format qcow2

    say "cloud-init seed"
    seed=$(mktemp -d)
    trap 'rm -rf "$seed"' EXIT
    cat > "$seed/user-data" <<EOF
#cloud-config
hostname: $DOMAIN
users:
  - name: howl
    groups: [wheel]
    sudo: "ALL=(ALL) NOPASSWD:ALL"
    shell: /bin/bash
    lock_passwd: false
    passwd: "$(openssl passwd -6 howl)"
    ssh_authorized_keys:
      - $(cat "$KEY.pub")
chpasswd:
  expire: false
  users:
    - {name: root, password: "$(openssl passwd -6 root)", type: hash}
ssh_pwauth: false
growpart:
  mode: auto
  devices: ['/']
resize_rootfs: true
EOF
    cat > "$seed/meta-data" <<EOF
instance-id: $DOMAIN-01
local-hostname: $DOMAIN
EOF
    xorriso -as mkisofs -o "$seed/seed.iso" -V CIDATA -J -r \
        "$seed/user-data" "$seed/meta-data" >/dev/null 2>&1
    virsh vol-create-as "$POOL" "$SEED_VOL" "$(stat -c%s "$seed/seed.iso")" --format raw
    virsh vol-upload --pool "$POOL" "$SEED_VOL" "$seed/seed.iso"
fi

# One fixed address, so the instructions can name a port and a host rather than
# a step that says "find out where it landed".
virsh net-update default add ip-dhcp-host \
    "<host mac='$MAC' name='$DOMAIN' ip='$ADDRESS'/>" --live --config >/dev/null 2>&1 || true

if ! virsh dominfo "$DOMAIN" >/dev/null 2>&1; then
    say "defining $DOMAIN"
    # `bochs` and not `virtio-gpu`: this host's qemu has no virtio-gpu and no
    # qxl -- `virsh domcapabilities` offers vga, cirrus, vmvga, bochs and ramfb
    # and nothing else. bochs-display gives the guest a `bochs-drm` card0 that
    # Hyprland can do atomic modesetting on through kms_swrast, and that VNC
    # scans out -- which `vkms` never did, and which is the whole reason
    # `omahouse-poc` could be tested and not watched.
    virt-install \
        --connect qemu:///system \
        --name "$DOMAIN" \
        --metadata title="omahouse on real Omarchy",description="project=omahouse;purpose=manual-demo;disposable=true" \
        --memory 8192 --vcpus 4 --cpu host-passthrough --machine q35 \
        --disk "vol=$POOL/$DISK_VOL,bus=virtio" \
        --disk "vol=$POOL/$SEED_VOL,device=cdrom,bus=sata" \
        --os-variant archlinux \
        --network "network=default,model=virtio,mac=$MAC" \
        --graphics vnc,listen=127.0.0.1 \
        --video model.type=bochs,model.vram=65536 \
        --console pty,target_type=serial \
        --channel unix,target.type=virtio,target.name=org.qemu.guest_agent.0 \
        --rng /dev/urandom \
        --import --noautoconsole
fi

virsh domstate "$DOMAIN" | grep -q running || virsh start "$DOMAIN"

say "waiting for ssh on $ADDRESS"
for _ in $(seq 1 90); do guest true 2>/dev/null && break; sleep 4; done
[ "$(guest 'uname -n')" = "$DOMAIN" ] || {
    echo "the machine at $ADDRESS does not call itself $DOMAIN" >&2; exit 1; }

# -- 2. Omarchy ---------------------------------------------------------------

say "omarchy repository and keyring"
guest 'sudo bash -s' <<'EOSH'
set -e
if ! grep -q '^\[omarchy\]' /etc/pacman.conf; then
    # SigLevel = Never for exactly one transaction: the keyring that makes
    # signature checking possible cannot itself be checked against a keyring
    # that is not installed yet. It comes straight back out below.
    printf '\n[omarchy]\nSigLevel = Never\nServer = https://pkgs.omarchy.org/stable/$arch\n' >> /etc/pacman.conf
    pacman -Sy --noconfirm
    pacman -S --noconfirm --needed omarchy-keyring
    pacman-key --populate omarchy
    sed -i '/^\[omarchy\]$/,/^Server/ { /^SigLevel = Never$/d }' /etc/pacman.conf
fi
pacman -Sy --noconfirm
EOSH

say "omarchy packages"
# What is left out, and why:
#   limine, limine-*-hook, snapper  -- the bootloader and its snapshots. Their
#     hooks would rewrite the boot of a cloud image that does not boot that way,
#     and none of it is anything omahouse measures. `--assume-installed` is what
#     lets `omarchy` install without them.
#   plymouth                        -- a boot splash, and it wants an initramfs
#     rebuild of a kernel this image installs from a different bootloader.
#   docker, dotnet, cups, printing, bluez, avahi, ufw, fcitx5, obs, kdenlive,
#   libreoffice, obsidian, localsend, moonlight, nautilus, mpv, evince, ruby,
#   llvm, clang, tesseract and the rest of the 150 -- weight with nothing to say
#     about app scopes, session slices, notifications or logind.
# What is kept is the session: hyprland, uwsm, quickshell (the bar AND the
# notification daemon AND the polkit agent), sddm with the Omarchy greeter, the
# portals, foot through xdg-terminal-exec, chromium, and the fonts the shell
# draws itself with.
guest 'sudo bash -s' <<'EOSH'
set -e
pacman -S --noconfirm --needed \
  --assume-installed limine --assume-installed limine-mkinitcpio-hook \
  --assume-installed limine-snapper-sync --assume-installed snapper \
  --assume-installed plymouth \
  omarchy omarchy-settings \
  hyprland uwsm quickshell sddm \
  wireplumber pipewire pipewire-pulse gnome-keyring \
  xdg-desktop-portal-hyprland xdg-desktop-portal-gtk xdg-terminal-exec foot \
  chromium networkmanager udiskie \
  wl-clipboard grim slurp brightnessctl pamixer \
  noto-fonts noto-fonts-emoji woff2-font-awesome ttf-jetbrains-mono-nerd-basic \
  yaru-icon-theme gnome-themes-extra \
  qt6-base qt6-declarative qt6-tools base-devel \
  btop fastfetch nvim git jq gum less bash-completion mesa vulkan-swrast \
  libnotify
EOSH

say "omarchy post-install, minus the bootloader and the printer"
guest 'sudo bash -s' <<'EOSH'
set -e
export OMARCHY_PATH=/usr/share/omarchy OMARCHY_INSTALL=/usr/share/omarchy/install
bash "$OMARCHY_INSTALL/config/theme-system.sh"
bash "$OMARCHY_INSTALL/config/browser-policy.sh"
bash "$OMARCHY_INSTALL/config/increase-lockout-limit.sh"
bash "$OMARCHY_INSTALL/config/lockscreen-pam.sh" || true
bash "$OMARCHY_INSTALL/login/sddm.sh"
bash "$OMARCHY_INSTALL/post-install/udev.sh" || true
systemctl enable sddm.service
systemctl set-default graphical.target

# NetworkManager, because the Omarchy bar's network indicator talks to it and
# nothing else. Both DHCP clients on one link is two DHCP clients on one link,
# so networkd steps off as NM steps on.
systemctl disable --now systemd-networkd.socket systemd-networkd.service >/dev/null 2>&1 || true
systemctl mask NetworkManager-wait-online.service >/dev/null 2>&1 || true
systemctl enable NetworkManager.service
rm -f /etc/systemd/network/10-cloud-init-eth0.network

# The clock and the encoding. Neither is decoration: the ledger of docs/design.md §4 is
# one file per *local* date, so a machine on UTC turns the day over at nine in
# the evening; and Qt on a C locale prints a paragraph about it on every run,
# into the journal `omahouse` writes its one important line to.
sed -i 's/^#\(en_US.UTF-8 UTF-8\)/\1/; s/^#\(pt_BR.UTF-8 UTF-8\)/\1/' /etc/locale.gen
locale-gen
localectl set-locale LANG=pt_BR.UTF-8
timedatectl set-timezone America/Sao_Paulo

# cloud-init has done the one thing it was for. Left enabled it would fight the
# network configuration above on the next boot.
touch /etc/cloud/cloud-init.disabled
systemctl disable cloud-init.service cloud-init-local.service \
    cloud-config.service cloud-final.service >/dev/null 2>&1 || true
EOSH

# -- 3. the two accounts ------------------------------------------------------

say "howl the operator, julia the subject"
guest 'sudo bash -s' <<'EOSH'
set -e
# docs/design.md §1: the operator is whoever is in wheel, and the subject is an account
# with no privilege at all. julia is not in wheel and `omahouse profile add`
# would refuse her if she were.
id julia >/dev/null 2>&1 || useradd -m -s /bin/bash julia
echo 'julia:julia' | chpasswd

# /etc/skel arrived with omarchy-settings, after cloud-init had already made
# howl. Both homes get it, and `cp -n` leaves anything already there alone.
for u in howl julia; do
    home=$(getent passwd "$u" | cut -d: -f6)
    cp -rn /etc/skel/. "$home"/ 2>/dev/null || true
    chown -R "$u:$u" "$home"
    sudo -u "$u" -H env HOME="$home" OMARCHY_SETUP_CONTEXT=iso-chroot bash -c '
        mkdir -p ~/.config/omarchy/themes ~/.local/state/omarchy
        OMARCHY_THEME_HEADLESS=1 omarchy-theme-set "Tokyo Night" || true
        mkdir -p ~/.config/btop/themes
        ln -snf "$HOME/.local/state/omarchy/current/theme/btop.theme" \
            ~/.config/btop/themes/current.theme
        bash /usr/share/omarchy/install/user/default-keyring.sh
    ' || true
done

# The Omarchy SDDM theme has no user picker -- it is a password box for
# `userModel.lastUser` and nothing else. So the greeter is pointed at julia,
# who is the account this machine is for; howl gets in over ssh, on a tty, or by
# `vm-login-as howl` below.
install -d -o sddm -g sddm /var/lib/sddm
cat > /var/lib/sddm/state.conf <<EOF
[Last]
User=julia
Session=/usr/local/share/wayland-sessions/omarchy.desktop
EOF
chown sddm:sddm /var/lib/sddm/state.conf

# The greeter guard. NOT part of omahouse, and labelled so wherever it appears.
#
# Measured on this machine: SDDM 0.21.0 reads a session ended by `loginctl
# terminate-user` as `SDDM::Auth::ERROR_INTERNAL "Process crashed"` and then
# does nothing at all -- no new display, no greeter. What is left is the dead
# session's VT with a black screen on it, and `loginctl show-seat seat0`
# answering `Sessions=` with nothing after it.
#
# docs/design.md round 2 measured `terminate-user` against a tty1 autologin,
# where getty respawned the session in seconds; the worry there was that the
# user came *back* too fast. Under a display manager -- which is what Omarchy
# actually ships -- the opposite happens and nobody comes back at all, not even
# to be refused. Without this the demo ends at a black rectangle.
cat > /usr/local/bin/omahouse-vm-greeter-guard <<'EOF'
#!/usr/bin/env bash
# Restart sddm when seat0 has been empty for three polls running. Three and not
# one, because handing a seat from the greeter to the user passes through zero.
set -euo pipefail
empty=0
while sleep 3; do
    systemctl is-active --quiet sddm.service || { empty=0; continue; }
    if [ -z "$(loginctl show-seat seat0 -p Sessions --value 2>/dev/null)" ]; then
        empty=$((empty + 1))
        if [ "$empty" -ge 3 ]; then
            logger -t omahouse-vm-greeter-guard "seat0 empty; restarting sddm"
            systemctl restart sddm.service
            empty=0
            sleep 10
        fi
    else
        empty=0
    fi
done
EOF
chmod 0755 /usr/local/bin/omahouse-vm-greeter-guard
cat > /etc/systemd/system/omahouse-vm-greeter-guard.service <<'EOF'
[Unit]
Description=Bring the SDDM greeter back after loginctl terminate-user (VM demo workaround)
After=sddm.service

[Service]
ExecStart=/usr/local/bin/omahouse-vm-greeter-guard
Restart=always
RestartSec=5

[Install]
WantedBy=graphical.target
EOF
systemctl daemon-reload
systemctl enable omahouse-vm-greeter-guard.service

cat > /usr/local/bin/vm-login-as <<'EOF'
#!/usr/bin/env bash
# Point the Omarchy greeter at an account.
#
# The Omarchy SDDM theme authenticates `userModel.lastUser` and offers no way to
# type a different name, so switching graphically means telling SDDM who the
# last user was. This is a fact about real Omarchy and not about this VM.
set -euo pipefail
[ "${1:-}" ] || { echo "usage: vm-login-as <user>" >&2; exit 1; }
id "$1" >/dev/null
sudo sed -i "s|^User=.*|User=$1|" /var/lib/sddm/state.conf
sudo systemctl restart sddm
echo "the greeter now asks for $1's password"
EOF
chmod 0755 /usr/local/bin/vm-login-as
EOSH

# -- 4. omahouse, out of this working tree ------------------------------------

say "building and installing omahouse from $ROOT"
tarball=$(mktemp /tmp/omahouse-src-XXXXXX.tgz)
trap 'rm -f "$tarball"' EXIT
# `-h` so vm/omahouse.install arrives as the file it points at.
tar czhf "$tarball" -C "$ROOT" \
    --exclude=build --exclude=build-tests --exclude=.git --exclude=__pycache__ .
scp -q -i "$KEY" -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
    -o LogLevel=ERROR "$tarball" "howl@$ADDRESS:/tmp/omahouse-src.tgz"

guest 'bash -s' <<'EOSH'
set -e
rm -rf ~/omahouse-src && mkdir -p ~/omahouse-src
tar xzf /tmp/omahouse-src.tgz -C ~/omahouse-src
# makepkg out of the tree and not in it: $srcdir would otherwise be a child of
# what prepare() copies.
mkdir -p ~/omahouse-pkg
cp -L ~/omahouse-src/vm/PKGBUILD ~/omahouse-src/vm/omahouse.install ~/omahouse-pkg/
cd ~/omahouse-pkg
OMAHOUSE_TREE=/home/howl/omahouse-src makepkg -f
sudo pacman -U --noconfirm --overwrite '*' omahouse-*-x86_64.pkg.tar.zst
EOSH

# -- 5. the profile somebody can watch happen ---------------------------------

say "julia's profile"
# Short enough to see, and long enough to sit down first. The ids are the ones
# a real Omarchy session actually produces, which is not what the poc predicted:
# one Chromium window makes TWO scopes with two different ids --
# `app-Hyprland-chromium-*.scope` for the twelve child processes and
# `app-org.chromium.Chromium-*.scope` for the one that owns the window -- so
# both are allowed and both carry the three minute budget. Allowing only
# `chromium`, under `default: deny`, would have had the daemon close the browser
# for the wrong reason two seconds after it opened.
guest 'sudo bash -s' <<'EOSH'
set -e
omahouse profile add julia --name "Júlia" 2>/dev/null || true
omahouse profile default julia --deny
omahouse allow julia xdg-terminal-exec
omahouse allow julia omahouse
omahouse allow julia chromium --limit 3m
omahouse allow julia org.chromium.Chromium --limit 3m
# Omarchy's own. `default/hypr/autostart.lua` launches these two through
# `uwsm-app --`, so they get app scopes of their own under app.slice and are
# judged exactly like a game would be. Measured: with `default: deny` and no
# rule for it, omahouse SIGTERMs `omarchy-hyprland-monitor-watch` two seconds
# after login -- correctly by the model, and wrongly by any reading of what the
# machine is for. An allowlist on real Omarchy has to name Omarchy.
omahouse allow julia omarchy-hyprland-monitor-watch
omahouse allow julia udiskie
omahouse limit julia --session 10m
omahouse profile enforce julia --on
omahouse profile show julia
EOSH

say "done. $DOMAIN is at $ADDRESS"
