# PoC — o que a primeira medição achou

`observe.py`, 12 s, sessão Omarchy real do usuário `howl`, sem root e sem
enviar sinal nenhum. Saída bruta em `/tmp/omahouse-baseline-howl.json`.

## O número que derruba o desenho antigo

**57 executáveis distintos** numa sessão comum, com um navegador, um editor e
um terminal abertos. O `spec.md` original supunha uma baseline de meia dúzia de
nomes escrita à mão. A realidade é ~45 processos de encanamento, e a lista
mudaria a cada atualização do Omarchy.

Pior, três casos que caminho de binário não resolve:

| exe | processos | por quê quebra |
|---|---|---|
| `/usr/lib/chromium/chromium` | 21 | um app, um `exe`, vinte e um PIDs |
| `/usr/share/code/code` | 13 | idem |
| `/usr/bin/bash` | 26 | ora encanamento, ora o terminal do usuário |

Um `exe` não identifica um app, não agrupa a árvore dele e não separa app de
encanamento. `bash` é o caso terminal: não dá para permitir nem negar.

Três binários rodando de dentro de `~/.local/` numa sessão comum
(`mise/installs/claude`, `omp`, `.local/bin/okt`) confirmam que o furo do
`spec.md` §10 não é teórico — usuário normal já roda binário de casa.

Nenhum Flatpak nesta máquina, então o risco do `bwrap` colapsar todos os
flatpaks num caminho só **segue não verificado**. Precisa de uma máquina com
Flatpak antes de virar afirmação.

## O que o systemd já resolveu

O `uwsm` do Omarchy separa app de encanamento por construção:

```
session.slice/wayland-wm@hyprland.desktop.service   Hyprland, quickshell
session.slice/pipewire.service                      pipewire
app.slice/app-graphical.slice/app-Hyprland-chromium-031bdc27.scope
app.slice/app-graphical.slice/app-Hyprland-xdg\x2dterminal\x2dexec-*.scope
app.slice/app-code-3579042.scope
```

Verificado nesta máquina: 10 escopos em `app.slice`, os 21 processos do
Chromium num só, e `cgroup.kill` presente em cada escopo.

O `bash` do terminal está no escopo do terminal — que é a resposta certa, e a
que nenhuma lista de executáveis daria.

## O que mudou no spec

- O seletor de regra e de orçamento passa a ser a identidade do escopo, não o
  caminho do executável.
- `baseline.json` foi removido. A regra virou estrutural: `session.slice` nunca
  é tocada, só `app.slice` é julgada.
- Fechar um app virou uma escrita em `cgroup.kill`, não uma caçada a PIDs.

## O que ainda não foi provado

1. Flatpak — sem amostra.
2. `loginctl terminate-user` encerrando uma sessão Hyprland de verdade.
3. `notify-send` de root para o barramento de outro usuário.
4. Um app lançado por `hyprctl dispatch exec` ganha escopo próprio, ou herda?
5. Escopos de app numa conta recém-criada, sem nada instalado.

Os itens 2 e 3 pedem um segundo usuário. O 1 e o 5 pedem a caixa do
`testing.md`. O 4 é uma medição de dez segundos nesta mesma máquina.

---

# Rodada 2 — VM Arch com Hyprland real

Feita na VM `omahouse-poc` (libvirt, sem sudo no host, descartável). Arch cloud
image + `hyprland 0.56.2` + `uwsm 0.26.7` + `mako`, autologin de `julia` na
tty1, Hyprland em `vkms` (GPU virtual do kernel — o host não tinha `virtio-gpu`
no qemu). Sessão de *seat* real: `loginctl` lista `julia seat0 tty1`.

O layout de cgroup da VM bateu com o do host, então o que segue vale para
Omarchy de verdade.

## Confirmado

**`cgroup.kill` isola.** Escopo `app-Hyprland-sleep-7865852f.scope`, 1 PID → 0
PIDs numa escrita. Hyprland PID 484 antes e depois, intacto. O `spec.md` §5
está certo nesse ponto.

**Root notifica a sessão de outro usuário.** `systemd-run --uid=1001
--setenv=DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1001/bus notify-send ...`
e o `makoctl list` do lado de dentro mostrou `Notification 1: Faltam 5
minutos`. Ponta a ponta, com daemon de notificação de verdade.

## Corrigido — o escopo depende de COMO o app foi lançado

Dois `sleep 600` idênticos, dois cgroups:

| lançamento | cgroup |
|---|---|
| `hyprctl dispatch exec` | `session.slice/wayland-wm@hyprland.desktop.service` |
| `uwsm app --` | `app.slice/app-graphical.slice/app-Hyprland-sleep-7865852f.scope` |

O primeiro cai **dentro da unidade do compositor**. Para o omahouse ele é
invisível: não dá para contabilizar (é o mesmo cgroup do Hyprland) e não dá
para fechar (o `cgroup.kill` levaria a sessão junto).

Verificado no host que o Omarchy embrulha tudo em `uwsm-app --`
(`/usr/share/omarchy/default/hypr/helpers.lua:109`, e os `omarchy-launch-*`).
Então o modelo vale para os apps do Omarchy. O furo é para quem escreve um
`exec` cru à mão num keybinding, e para o que nasce de dentro de um terminal.

**Consequência:** o omahouse precisa relatar o que não consegue ver, em vez de
fingir que não existe. Um perfil cujo `session.slice` tem processo demais
merece um aviso no `status`, não silêncio.

## Corrigido — autologin derrota o `logout`

`loginctl terminate-user julia` encerrou as sessões 1 e 2 em segundos. Um
instante depois o autologin da tty1 **reergueu a sessão** (sessões 21 e 22,
Hyprland vivo de novo).

Numa máquina com autologin — que é o caso comum de um micro de uso doméstico —
`onExhausted: "logout"` é teatro: o usuário volta na hora.

**Consequência:** `logout` sozinho não é ação de esgotamento. Precisa vir com
um bloqueio de re-login. As `hours` do §12, que eu tinha tratado como enfeite
para depois, são na verdade parte do mecanismo: `pam_time.so` (ou um módulo
PAM próprio consultando o ledger) é o que impede a volta. Sem isso, a única
ação de esgotamento honesta para a sessão é `lock`.

## Ainda sem resposta

- **Flatpak** — não instalei na VM; segue sem amostra. É o último furo grande.
- Escopos numa conta recém-criada, sem nada instalado.

## A VM

`omahouse-poc` fica **desligada** e definida, com o disco em
`/var/lib/libvirt/images/omahouse-poc.qcow2` (overlay sobre
`omahouse-arch-base.qcow2`, 533 MB). É a semente da base preparada do
`testing.md` §3 — instalar tudo de novo custa uns 20 minutos.

Para descartar:

```bash
export LIBVIRT_DEFAULT_URI=qemu:///system
virsh undefine omahouse-poc --nvram
virsh vol-delete --pool images omahouse-poc.qcow2
virsh vol-delete --pool images omahouse-arch-base.qcow2   # se não for reusar
```

---

# Rodada 3 — os dois últimos furos, fechados

## Flatpak: o cgroup ganha, o `exe` perde

`uwsm app -- flatpak run --command=sleep org.freedesktop.Platform//24.08 900`:

| PID | comm | `/proc/pid/exe` | cgroup |
|---|---|---|---|
| 3880 | bwrap | `/usr/bin/bwrap` | `app-flatpak-org.freedesktop.Platform-2351381583.scope` |
| 3911 | bwrap | `/usr/bin/bwrap` | idem |
| 3915 | bwrap | `/usr/bin/bwrap` | idem |
| 3916 | sleep | `/usr/bin/sleep` | idem |

Quatro processos, três com `exe=/usr/bin/bwrap` e um com `/usr/bin/sleep`.
Caminho de binário não identifica nada — era exatamente o risco não verificado.

O escopo carrega o **app id do flatpak** no nome
(`app-flatpak-org.freedesktop.Platform-…`), e agrupa os quatro processos. O
modelo de identidade por cgroup do §5 cobre flatpak sem nenhum caso especial.

## Bloqueio de re-login: `pam_listfile` resolve

Em `/etc/pam.d/system-login`:

```
account required pam_listfile.so item=user sense=deny \
        file=/etc/omahouse/blocked onerr=succeed
```

`onerr=succeed` é obrigatório: arquivo ausente ou ilegível não pode trancar
ninguém para fora da própria máquina.

Medido:

1. `julia` em `/etc/omahouse/blocked` + `loginctl terminate-user julia`
2. 25 s depois — 0 sessões, Hyprland morto, e a tty1 registrando
   `pam_listfile(login:account): Refused user julia for service login`
3. tirando o nome do arquivo, a sessão voltou sozinha em 20 s

O `logout` do §2 vira, então: escreve no arquivo, `terminate-user`, e apaga do
arquivo na virada do dia. Sem módulo PAM próprio — `pam_listfile` é padrão.

## Nada mais em aberto

As cinco perguntas da rodada 1 estão respondidas. O modelo do `spec.md` está
validado numa sessão Omarchy-like real.

---

# Rodada 4 — o shim de lançamento apaga a identidade

Achado ao rodar o `omahouse status` da etapa 4 contra uma sessão real.

Sete escopos desta máquina se chamam `app-Hyprland-gtk\x2dlaunch-*.scope`. O
`Description` da unidade também diz apenas `gtk-launch`. Mas os processos lá
dentro são `/usr/share/code/chrome_crashpad_handler` — **é o VS Code**.

Mesma forma para o terminal: o escopo é `xdg-terminal-exec`, não o nome do
emulador.

É o problema do `bwrap` da rodada 1 reaparecendo uma camada acima: quando o app
é lançado por um shim, o escopo herda o nome do shim, e todos os apps lançados
por aquele caminho colapsam num id só.

**Consequência prática:** liberar `gtk-launch` numa allowlist é liberar um
conjunto desconhecido e variável de programas. É um furo de verdade, não uma
imprecisão de relatório.

**Sinal secundário disponível:** o `exe` dominante dos processos dentro do
escopo. Ele resolve o caso do shim (`/usr/share/code/…` → VS Code) justamente
onde o id falha, e o id resolve o caso do flatpak (`org.freedesktop.Platform`)
justamente onde o `exe` falha. Os dois erram em situações opostas.

Ainda **não** implementado. O `evaluate` continua casando por id e permanece
puro; o `exe` dominante é informação de configuração, para o operador não
liberar às cegas, e não critério de decisão.

## Segundo ponto cego, distinto

Escopos sob `app.slice` cujo nome o parser recusa — aqui, 23 do tipo
`tmux-spawn-<uuid>.scope`, com 71 processos. Diferente do `session.slice`:
esses o omahouse **vê e consegue fechar**, só não tem id para casar com regra.
O `status` os reporta em separado.
