# A VM `omahouse-omarchy`

> Este documento é para **usar** a VM. Quem precisa trabalhar nela — rodar os
> casos automatizados, dirigir o desktop por `ydotool`, capturar telas ou
> reconstruir a máquina — quer o [`docs/vm-runbook.md`](../docs/vm-runbook.md).

Omarchy 4.0.2 de verdade — Hyprland sob `uwsm`, a barra do `quickshell`, o SDDM
com o greeter do Omarchy — com o omahouse instalado por pacote e a `julia` já
sob regras. É para abrir e usar com a mão, não para rodar teste.

A VM de teste automatizado é outra (`omahouse-poc`, `mise run test:vm`). Esta
não encosta naquela.

---

## 1. Ligar e desligar

```bash
export LIBVIRT_DEFAULT_URI=qemu:///system    # em toda sessão de terminal

virsh start omahouse-omarchy                 # liga
virsh shutdown omahouse-omarchy              # desliga com jeito
virsh destroy omahouse-omarchy               # puxa o fio, se travar
virsh domstate omahouse-omarchy              # ligada ou desligada?
```

Ela sobe em uns 30 segundos e cai direto no greeter. O endereço é fixo:
**192.168.122.60**.

---

## 2. Ver a tela

A tela sai por **VNC em `127.0.0.1:5900`** (`virsh vncdisplay omahouse-omarchy`
diz `127.0.0.1:0`, que é a porta 5900).

Você já tem o **remmina** instalado — não precisa instalar nada:

```bash
remmina -c vnc://127.0.0.1:5900
```

Se preferir o `virt-viewer`, ele pega o teclado inteiro (inclusive a tecla
SUPER, que é metade do Omarchy) e tem tela cheia no `F11`, mas exige instalar:

```bash
sudo pacman -S virt-viewer
virt-viewer --connect qemu:///system omahouse-omarchy
```

> No remmina, marque **"Capturar todas as teclas"** na conexão, senão o
> `SUPER` vai para o seu Hyprland em vez de ir para o da VM — e o `SUPER` é
> metade do Omarchy.

> **Por que funciona.** O qemu desta máquina não tem `virtio-gpu` nem `qxl` —
> só `vga`, `cirrus`, `vmvga`, `bochs` e `ramfb`. A VM usa **`bochs`**, que dá
> ao convidado um `/dev/dri/card0` do driver `bochs-drm`; o Hyprland faz
> modesetting atômico nele por `kms_swrast` e o VNC enxerga o que ele desenha.
> É o que o `vkms` da `omahouse-poc` nunca fez.

---

## 3. As contas

| conta | senha | o que é |
|---|---|---|
| `julia` | `julia` | o sujeito. Sem privilégio, sob regras. É quem o greeter oferece. |
| `howl` | `howl` | o operador. No `wheel`, `sudo` sem senha. |
| `root` | `root` | só para emergência no console. |

O operador entra por ssh, que é como um administrador entraria:

```bash
ssh -i ~/.ssh/id_vms howl@192.168.122.60
```

**O greeter do Omarchy não tem seletor de usuário.** O tema `omarchy` do SDDM é
uma caixa de senha para o último usuário e nada mais — isso é o Omarchy de
verdade, não uma limitação desta VM. Para entrar graficamente como `howl`:

```bash
ssh -i ~/.ssh/id_vms howl@192.168.122.60 vm-login-as howl
```

e para devolver o greeter à `julia`, `vm-login-as julia`. Sem ssh, o
`Ctrl+Alt+F3` dentro da VM dá um console de texto onde `howl` entra com senha
(`F1` é o greeter, `F2` em diante é a sessão gráfica de quem entrou).

---

## 4. O roteiro

A `julia` tem: **sessão de 10 minutos**, **Chromium de 3 minutos**, allowlist
(`default: deny`) e `enforce: true`. Os avisos saem a 10, 5 e 1 minuto do fim, e
entre o "acabou" e o fechamento há 20 segundos de carência.

Os 10 minutos começam a correr **no login**, porque o `udiskie` que o próprio
Omarchy sobe já é um escopo de app vivo. Ou seja: dá para sentar, olhar em volta
e ainda assim ver o tempo acabar. Se você quiser mais folga, veja §6 antes de
entrar.

1. **Ligue e abra a tela.** O greeter do Omarchy pede a senha da `julia`. Digite
   `julia` e Enter. Sobe a sessão Hyprland com a barra do Omarchy.

2. **Abra um programa liberado.** `SUPER + Enter` abre o terminal;
   `SUPER + Shift + B` abre o Chromium.

   Use as **teclas de atalho**, não o menu (`SUPER + Espaço` → Apps). O menu do
   Omarchy lança tudo por `gtk-launch`, e aí o escopo se chama `gtk-launch` e
   não o nome do programa — sob `default: deny` o omahouse fecha na hora. Veja
   §5.

3. **Veja o aviso chegar.** Cerca de dois minutos depois do Chromium abrir, uma
   notificação do Omarchy diz `1 minute left — chromium closes at …`. Um minuto
   depois vem `Time is up — chromium closes in 20 seconds`.

4. **Veja o programa fechar.** Passados os 20 segundos, o Chromium some — e o
   terminal, a barra e a sessão continuam onde estavam. Isso é o ponto todo: só
   o escopo do app foi morto, e a `session.slice` nem foi tocada.

   De fora, para acompanhar:
   ```bash
   ssh -i ~/.ssh/id_vms howl@192.168.122.60 'omahouse status julia'
   ssh -i ~/.ssh/id_vms howl@192.168.122.60 'sudo journalctl -t omahouse -f'
   ```

5. **Veja a sessão cair.** Aos 10 minutos, a sessão termina e a tela volta para
   o greeter.

   > A tela fica **preta por uns dez segundos** antes do greeter voltar, e isso
   > é um achado, não um defeito da VM. O SDDM 0.21 lê uma sessão encerrada por
   > `loginctl terminate-user` como `ERROR_INTERNAL "Process crashed"` e não faz
   > mais nada — nem greeter, nem display novo. Quem traz o greeter de volta
   > aqui é o `omahouse-vm-greeter-guard.service`, um remendo desta VM que
   > reinicia o SDDM quando o `seat0` fica sem sessão nenhuma. Ele **não** faz
   > parte do omahouse. Num Omarchy de fábrica, `onExhausted: "logout"` deixa a
   > máquina numa tela preta.

6. **Veja o login ser recusado.** Digite a senha da `julia` de novo. O greeter
   pisca e recusa — o nome dela está em `/etc/omahouse/blocked` e o
   `pam_listfile` do `/etc/pam.d/system-login` não deixa entrar. Isso vale no
   greeter e no console de texto.

7. **Entre como `howl` e dê mais tempo.** Por ssh:
   ```bash
   ssh -i ~/.ssh/id_vms howl@192.168.122.60
   omahouse status julia
   sudo omahouse grant julia --session 10m
   ```

8. **Veja a `julia` voltar.** Em até dois segundos o nome sai do arquivo de
   bloqueio, e a senha dela entra de novo no greeter.

---

## 5. O `omahouse-studio`

**Como `julia`, de dentro da sessão dela** (é a cara de leitura: o que sobrou do
dia dela e nada mais). Abra o terminal com `SUPER + Enter` e:

```bash
uwsm app -- omahouse.desktop
```

> **Não abra pelo menu do Omarchy.** O `SUPER + Espaço` → *Apps* → "Regras da
> Casa" existe e tem o ícone certo, mas o menu lança por `gtk-launch`: o escopo
> que nasce se chama `app-Hyprland-gtk\x2dlaunch-*.scope`, o id é `gtk-launch` e
> não `omahouse`, e com `default: deny` o omahouse o fecha antes da janela
> aparecer. É o furo da rodada 4 do `poc/findings.md` acontecendo de verdade:
> pelo menu, **nenhum** programa dá para liberar pelo nome — só `gtk-launch`,
> que é todos. Se quiser usar o menu:
> `sudo omahouse allow julia gtk-launch` — e saiba que isso libera o menu
> inteiro.

**Como `howl`** (a cara de operador: todos os perfis, os saldos ao vivo, a lista
de programas e o botão de liberar tempo) — precisa de uma sessão gráfica dele,
então `vm-login-as howl`, entrar no greeter, abrir o terminal e o mesmo comando.
`howl` não tem perfil, então nada do que ele abre é julgado.

A janela é inteira de teclado:

| tecla | o que faz |
|---|---|
| `?` | a lista de teclas |
| `j` `k` (ou setas) | desce, sobe |
| `l` ou `Enter` | abre o perfil sob o cursor |
| `h` ou `Esc` | volta |
| `1` `2` `3` | as pessoas · os programas de uma · o dia dela |
| `/` | filtra a lista |
| `:` | a paleta de comandos |
| `n` | novo perfil |
| `e` | liga e desliga os dentes (`enforce`) |
| `d` | allowlist ou denylist |
| `a` | libera um programa |
| `m` | minutos por dia daquele programa |
| `+` | mais tempo **hoje** |
| `s` | quanto dura o dia inteiro |
| `x` | tira da lista / tira dos livros |

Tudo que escreve passa por `pkexec omahouse …`, então o polkit pede a senha de
um administrador — a do `howl`. O agente de polkit é o do próprio
`omarchy-shell`.

---

## 6. Afrouxar os limites

Para usar a VM sem ser interrompido, por ssh como `howl`:

```bash
sudo omahouse limit julia --session 4h            # um dia inteiro
sudo omahouse limit julia --budget chromium=4h
sudo omahouse limit julia --budget org.chromium.Chromium=4h
```

Ou desligue os dentes de vez, que é o modo observação — conta e relata, não
fecha nada:

```bash
sudo omahouse profile enforce julia --off
```

Ou tire a allowlist do caminho, para que qualquer programa rode:

```bash
sudo omahouse profile default julia --allow
```

Para voltar ao roteiro:

```bash
sudo omahouse profile default julia --deny
sudo omahouse profile enforce julia --on
sudo omahouse limit julia --session 10m
sudo omahouse limit julia --budget chromium=3m
sudo omahouse limit julia --budget org.chromium.Chromium=3m
sudo rm -f /var/lib/omahouse/julia/$(date +%F).json   # zera o dia
sudo rm -f /etc/omahouse/blocked                      # e destranca o login
```

> Os dois orçamentos de Chromium não são engano. Uma janela do Chromium no
> Omarchy real produz **dois** escopos com **dois ids**: o
> `app-Hyprland-chromium-*.scope` com os doze processos filhos, e o
> `app-org.chromium.Chromium-*.scope` com o processo que é dono da janela.
>
> E `udiskie` e `omarchy-hyprland-monitor-watch` estão na allowlist porque são
> do **próprio Omarchy** — o `autostart.lua` dele os lança por `uwsm-app --`, e
> sem regra o omahouse os fecha dois segundos depois do login.

---

## 7. Se travar

| sintoma | o que fazer |
|---|---|
| a tela ficou preta e não volta | `sudo systemctl restart sddm` (o guard faz isso sozinho em uns 10 s) |
| a tela congelou | `Ctrl+Alt+F3` dentro da VM dá um console de texto; entre como `howl` e `sudo systemctl restart sddm` |
| não consegue mais entrar como `julia` | `ssh -i ~/.ssh/id_vms howl@192.168.122.60 'sudo rm -f /etc/omahouse/blocked'` |
| o daemon está mordendo demais | `sudo systemctl stop omahouse.service` — para religar, `sudo systemctl start omahouse.service` |
| o ssh não responde | `virsh console omahouse-omarchy` dá o console serial; `root` / `root` |
| a VM não responde a nada | `virsh destroy omahouse-omarchy && virsh start omahouse-omarchy` |
| quer começar do zero | `vm/provision-omarchy.sh --recreate` (uns 20 minutos) |

O que dizer da máquina, de fora, sem entrar nela:

```bash
virsh domifaddr omahouse-omarchy       # o endereço
virsh screenshot omahouse-omarchy /tmp/tela.png    # uma foto da tela
virsh send-key omahouse-omarchy --codeset linux KEY_J KEY_U KEY_L KEY_I KEY_A
virsh send-key omahouse-omarchy --codeset linux KEY_ENTER    # entra como julia
```

---

## 8. Descartar

```bash
export LIBVIRT_DEFAULT_URI=qemu:///system
virsh destroy omahouse-omarchy 2>/dev/null
virsh undefine omahouse-omarchy --nvram
virsh vol-delete --pool images omahouse-omarchy.qcow2
virsh vol-delete --pool images omahouse-omarchy-seed.iso
```

`omahouse-arch-base.qcow2` é a base de que ela sai; a `omahouse-poc` também
depende dela, então essa fica.
