*Português. Este é o documento de quem trabalha nas VMs; `vm/OMARCHY-VM.md` é o
de quem usa a VM de demonstração.*

# Runbook — as VMs, os testes e as capturas

O que fazer com as mãos para ligar as duas máquinas virtuais do omahouse, rodar
os casos automatizados, **dirigir o desktop da VM pelo teclado** e **tirar foto
da tela**. A parte de dirigir e capturar não estava escrita em lugar nenhum:
existia só como procedência nas legendas de `vm/shots/README.md` e da §2 de
`docs/screens.md`. Está aqui agora.

Onde ele se encaixa:

| documento | para quê |
|---|---|
| `testing.md` | a estratégia das três camadas, e por que a camada 2 é manual |
| `vm/OMARCHY-VM.md` | o roteiro de aceitação, para o dono **usar** a VM |
| `poc/findings.md` | o terreno medido, e de onde vieram as decisões de vídeo |
| **este** | ligar, dirigir, capturar, restaurar e reconstruir |

> **A regra deste documento.** Todo comando aqui foi executado nesta máquina.
> Onde não foi, está dito, com o motivo — veja a §10. A sessão que o escreveu
> foi a de **2026-09-04, entre 11:31 e 12:0x**, com `libvirt 12.6.0`,
> `qemu 11.1.0` e `ydotool 1.0.4` no convidado.

---

## 1. São duas VMs, e não uma

```
$ virsh list --all
 Id   Name               State
----------------------------------
 1    omakure-r3-linux   running
 16   omahouse-omarchy   running
 18   omahouse-poc       running
```

`omakure-r3-linux` **não é deste projeto**. Não se encosta nela.

| | `omahouse-poc` | `omahouse-omarchy` |
|---|---|---|
| o que roda | Arch + Hyprland + `uwsm` + `mako` | Omarchy 4.0.2 de verdade |
| entra por | autologin de `julia` na **tty1** | **SDDM** com o greeter do Omarchy |
| GPU | `vkms` (`modprobe` a cada boot) | `bochs` |
| vê-se pelo VNC? | **não** | sim |
| operador | `arch` | `howl` |
| endereço | DHCP, muda | fixo, **192.168.122.60** |
| disco | `/var/lib/libvirt/images/omahouse-poc.qcow2` | `…/omahouse-omarchy.qcow2` |
| para quê | os casos de `vm/run.sh`, **descartável** | demonstração e teste manual |

**Quem faz o quê.** `mise run test:vm` só toca na `omahouse-poc`, e o
`vm/manifest.toml` mais as três checagens do `vm/e2e.py` existem para garantir
isso — nome do domínio, disco, endereço que não é deste host, e `uname -n` do
outro lado do ssh. A `omahouse-omarchy` é dirigida à mão, e o
`vm/provision-omarchy.sh` é o único script que escreve nela.

**O que nunca se faz.** Não se roda `mise run test:vm` contra a
`omahouse-omarchy`: os casos apagam `/etc/omahouse/profiles.json`, esvaziam
`/var/lib/omahouse` e terminam a sessão do sujeito — o perfil da §4 do
`vm/OMARCHY-VM.md` iria junto. E não se usa a `omahouse-poc` para olhar tela: o
Hyprland dela desenha no `vkms`, que o VNC não enxerga (veja §9).

Elas dividem a mesma base, `omahouse-arch-base.qcow2`. Descartar uma não
descarta a outra, mas apagar a base quebra as duas.

---

## 2. Antes de qualquer comando

```bash
export LIBVIRT_DEFAULT_URI=qemu:///system
```

Em **toda** sessão de terminal. Sem isso o `virsh` vai para `qemu:///session`,
onde nenhum destes domínios existe, e a mensagem que ele dá ("domain not found")
parece uma VM perdida em vez de uma variável faltando.

**O libvirt funciona sem `sudo` aqui.** Esta conta está no grupo `libvirt`:

```
$ groups
howl libvirt docker input wheel
```

Então `virsh start`, `virsh screenshot`, `virsh domifaddr` e o resto rodam
direto. `sudo virsh` seria outra conexão e outro conjunto de domínios.

O que mais precisa estar no lugar, e está:

| | verificado |
|---|---|
| `~/.ssh/id_vms` | a chave das duas VMs |
| `remmina` | `1:1.4.43-3` — é o cliente VNC desta máquina |
| `imagemagick` | `7.1.2.30-1` — para recortar e comparar capturas |
| `virt-viewer` | **não instalado** |

---

## 3. Ligar, achar, entrar, desligar

### Ligar

```bash
virsh start omahouse-omarchy
virsh start omahouse-poc
virsh domstate omahouse-omarchy      # running | shut off
```

A `omahouse-omarchy` responde ssh **uns 30 segundos** depois do `start` (medido:
`start` às 11:31:29, ssh de pé às 11:32:0x) e cai direto no greeter.

### Achar o endereço

```
$ virsh domifaddr omahouse-omarchy
 Name     MAC address         Protocol   Address
------------------------------------------------------------
 vnet15   52:54:00:a2:56:7b   ipv4       192.168.122.60/24

$ virsh domifaddr omahouse-poc
 Name     MAC address         Protocol   Address
------------------------------------------------------------
 vnet17   52:54:00:7b:56:e1   ipv4       192.168.122.94/24
```

O da `omahouse-omarchy` é reservado no DHCP do libvirt pelo MAC e não muda. O da
`omahouse-poc` muda; o `vm/e2e.py` o descobre sozinho a cada run.

### Entrar

```bash
ssh -i ~/.ssh/id_vms howl@192.168.122.60     # omahouse-omarchy
ssh -i ~/.ssh/id_vms arch@192.168.122.94     # omahouse-poc
```

Vale um atalho de shell, porque este runbook inteiro é feito de comandos que vão
por esse cano:

```bash
g() { ssh -i ~/.ssh/id_vms -o StrictHostKeyChecking=no \
        -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR \
        -o BatchMode=yes howl@192.168.122.60 "$@"; }
```

As contas:

| VM | conta | senha | o que é |
|---|---|---|---|
| `omahouse-omarchy` | `julia` | `julia` | o sujeito, sob regras, sem privilégio |
| | `howl` | `howl` | o operador, no `wheel`, `sudo` sem senha |
| | `root` | `root` | só para o console serial |
| `omahouse-poc` | `julia` | — | o sujeito, autologin na tty1 |
| | `arch` | — | o operador, só por chave |

Confirmando de quem é a máquina antes de mandar qualquer coisa nela — é a mesma
checagem que o `e2e.py` faz, e vale fazer à mão também:

```
$ g 'uname -n'
omahouse-omarchy
```

### Desligar

```bash
virsh shutdown omahouse-omarchy      # com jeito, uns 15 s
virsh destroy  omahouse-omarchy      # puxa o fio, se travar
```

Deixe **as duas desligadas** quando terminar. Uma VM ligada e esquecida come
8 GB de RAM e continua contando tempo da `julia`.

---

## 4. Os casos automatizados

```bash
export LIBVIRT_DEFAULT_URI=qemu:///system
mise run test:vm                 # todos os casos, e desliga a máquina no fim
vm/run.sh --keep                 # deixa ligada, para olhar
vm/run.sh --case grace           # um caso, por um pedaço do nome
```

O que ele faz, em ordem: constrói o `omahouse` da árvore de trabalho
(`.scripts/build.sh`), prova três vezes que a máquina do outro lado é a
`omahouse-poc`, liga, espera o Hyprland, instala o binário e o empacotamento no
convidado, roda os cinco casos com um `reset` entre cada um, e desliga.

**Uns 4 minutos** com a VM já quente. Os casos:

| caso | o que prova |
|---|---|
| `close_fecha_o_escopo_e_nao_a_sessao` | orçamento esgotado fecha o escopo por SIGTERM, e por `cgroup.kill` quando o SIGTERM é ignorado |
| `grace_avisa_antes_de_fechar` | o aviso chega ao daemon de notificação da própria sessão antes de qualquer fechamento |
| `logout_bloqueia_o_reingresso` | a sessão acaba, o autologin é recusado pelo `pam_listfile`, e devolver tempo deixa entrar de novo |
| `servico_sobe_e_reergue` | o unit empacotado sobe, e um SIGKILL é respondido com processo novo |
| `session_slice_intocada` | `enforce` com allowlist vazia por um minuto, e o compositor nem percebe |

O último é o que justifica a camada inteira (`testing.md` §3): é o modo de falhar
que mataria o produto.

Saída real desta sessão:

```
omahouse — layer 2, omahouse-poc

  omahouse-poc is at 192.168.122.94
  it is omahouse-poc, and it is not this machine
  installing the build and the packaging

  close_fecha_o_escopo_e_nao_a_sessao
    a spent budget closes the scope by SIGTERM, or by cgroup.kill when that is ignored
      app-uwsm-omahouse\x2dpolite-4999d41d.scope gone 19s in, on its SIGTERM
      app-uwsm-omahouse\x2dstubborn-25e84d01.scope ignored it and went to cgroup.kill 23s in
      Hyprland still 536; session.slice unchanged; session ['1', '2'] active
    PASS  in 52s
```

**Por que ele não está no `mise run verify`.** O gate para na camada 1
(`testing.md` §5): `verify` é idempotente, roda em segundos e não liga máquina
nenhuma. Este liga uma VM, espera uma sessão gráfica, mata processos e encerra o
login de alguém — leva minutos, e o tipo de regressão que ele pega não chega uma
vez por hora. É manual e noturno, por decisão.

### Quando ele der `BLOCKED`

Pré-requisito ausente é `blocked` explícito, nunca caso pulado em silêncio
(`testing.md` §6). O que apareceu nesta sessão, no **primeiro** run, com a VM
recém-ligada:

```
  BLOCKED  mako would not start in the subject's session
```

O `mako` **está** instalado na `omahouse-poc` (`mako 1.11.0-1`) e sobe à mão sem
reclamar. O que houve é corrida: o `wait_for_the_session` do `e2e.py` espera o
*pid* do Hyprland e segue na hora seguinte, mas o pid existir não quer dizer que
o socket `wayland-1` já esteja aceitando conexão. O `mako` lançado nessa janela
morre, e dois segundos depois o `pid_of("mako")` não acha nada. **Rodar de novo
com a VM já quente passa** — foi o que se fez aqui. Veja a §9.

---

## 5. Dirigir o desktop pelo teclado — `ydotool`

Esta é a peça que faltava. O Hyprland da `omahouse-omarchy` só existe numa
sessão de *seat* de verdade, e por ssh não há teclado nenhum. O `ydotool` resolve
isso criando um **dispositivo de entrada virtual pelo `uinput`**, dentro do
convidado: para o `seat0` da VM ele é um teclado como qualquer outro, e tanto o
greeter do SDDM quanto o Hyprland recebem as teclas dele.

### 5.1 Instalar

```
$ g 'sudo pacman -S --noconfirm --needed ydotool'
Packages (1) ydotool-1.0.4-2
Total Installed Size:  0.04 MiB
installing ydotool...
```

Não vem na `omahouse-omarchy` de fábrica, e **não deve ficar**: é ferramenta de
teste, e a §7 manda tirá-la no fim.

### 5.2 Subir o `ydotoold`

O pacote **não traz unit de systemd** (`systemctl cat ydotoold.service` →
`No files found`). Suba como unidade transiente, com o socket num caminho fixo:

```bash
g 'sudo systemd-run --unit=ydotoold --description="ydotool daemon (teste manual)" \
     /usr/bin/ydotoold --socket-path=/run/ydotoold.socket'
```

```
Running as unit: ydotoold.service; invocation ID: f456b18cf27f4df884390af08dc1347f
```

O `--socket-path` não é capricho. Sem ele o `ydotoold` rodando como root põe o
socket em `/tmp/.ydotool_socket`, com modo `0600` — e o `ydotool` cliente, sob
`sudo`, procura o mesmo caminho por outra regra de ambiente. Fixar os dois lados
tira a adivinhação do meio.

`systemd-run --unit=` também é o que torna a limpeza uma linha:
`systemctl stop ydotoold`.

### 5.3 Alcançar o `seat` certo

Nada a configurar — mas confira, porque é aqui que uma tarde some. O dispositivo
virtual aparece no `seat0` sozinho:

```
$ g 'loginctl seat-status seat0'
seat0
Sessions: *c1
 Devices:
         ├─/sys/devices/pci0000:00/0000:00:01.0/drm/card0
         │ [MASTER] drm:card0
         ├─/sys/devices/platform/i8042/serio0/input/input1
         │ input:input1 "AT Translated Set 2 keyboard"
         ├─/sys/devices/virtual/input/input7
         │ input:input7 "ydotoold virtual device"
```

Se `ydotoold virtual device` não estiver nessa lista, o `ydotool` vai executar
sem erro e nada vai acontecer na tela — que é o modo de falhar mais caro que
existe aqui, porque parece que o atalho é que está errado.

O dispositivo **sobrevive a um `systemctl restart sddm`**: verificado nesta
sessão, o mesmo `ydotoold` alcançou o greeter novo depois do reinício.

### 5.4 Mandar texto

```bash
g 'sudo env YDOTOOL_SOCKET=/run/ydotoold.socket ydotool type --key-delay 25 julia'
```

`sudo env YDOTOOL_SOCKET=…` e não `sudo ydotool`: o `sudo` limpa o ambiente, e
sem a variável o cliente procura o socket no caminho padrão, não no que a §5.2
fixou.

`--key-delay 25` (ms) porque o padrão manda os eventos rápido demais para
algumas caixas de texto. 25 ms atravessou o greeter do SDDM inteiro sem perder
tecla.

### 5.5 Mandar tecla e combinação

A sintaxe é `<keycode>:<1 pressiona | 0 solta>`, com os códigos de
`/usr/include/linux/input-event-codes.h`. Os que importam:

| tecla | código |
|---|---|
| `Esc` | 1 |
| `Backspace` | 14 |
| `Enter` | 28 |
| `Shift` esquerdo | 42 |
| `B` | 48 |
| `Espaço` | 57 |
| `SUPER` esquerdo (`KEY_LEFTMETA`) | 125 |

```bash
# Enter
g 'sudo env YDOTOOL_SOCKET=/run/ydotoold.socket ydotool key 28:1 28:0'

# SUPER+Enter — abre o terminal
g 'sudo env YDOTOOL_SOCKET=/run/ydotoold.socket ydotool key 125:1 28:1 28:0 125:0'

# SUPER+Espaço — abre o menu do Omarchy
g 'sudo env YDOTOOL_SOCKET=/run/ydotoold.socket ydotool key 125:1 57:1 57:0 125:0'

# SUPER+Shift+B — abre o Chromium
g 'sudo env YDOTOOL_SOCKET=/run/ydotoold.socket ydotool key 125:1 42:1 48:1 48:0 42:0 125:0'
```

A ordem importa: modificador pressionado primeiro, solto por último, e o
aninhamento fechando de dentro para fora. Os três atalhos acima foram executados
nesta sessão e cada um abriu o que devia.

> Estes são os atalhos do Omarchy que importam para o roteiro. Use-os, e **não**
> o menu: o `SUPER+Espaço` lança tudo por `gtk-launch`, o escopo nasce com o
> nome do shim, e sob `default: deny` o omahouse fecha antes da janela aparecer.
> É o achado da rodada 4 do `poc/findings.md`; `vm/OMARCHY-VM.md` §5 conta o
> caso inteiro.

### 5.6 Esperar entre um passo e outro

Aqui é onde se perde tempo com corrida. O que foi medido nesta sessão:

| depois de | espere | por quê |
|---|---|---|
| `virsh start` | ssh responder (`~30 s`) | o greeter aparece depois do ssh |
| digitar a senha e `Enter` | **20 s** | Hyprland, `uwsm` e a barra do `quickshell` subirem |
| `SUPER+Enter` | **6 s** | o `xdg-terminal-exec` nascer e ganhar escopo |
| `SUPER+Espaço` | **4 s** | o menu desenhar |
| `SUPER+Shift+B` | **12 s** | o Chromium abrir os dois escopos |
| `systemctl restart sddm` | **15 s** | o greeter voltar |

Não durma um número: **espere um fato**, e só use o `sleep` como teto. Os fatos
que este runbook usa, todos legíveis por ssh:

```bash
g 'loginctl list-sessions --no-legend'      # a sessão existe, e em que seat
g 'pgrep -u julia -x Hyprland'              # o compositor está de pé
g 'omahouse status julia'                   # o escopo nasceu, e com que id
```

O `omahouse status` é o melhor dos três para esperar um app, porque ele responde
com o **id do escopo** — que é justamente o que uma regra é escrita sobre:

```
$ g 'omahouse status julia'
APP                    PIDS  VERDICT  SCOPE
chromium                 14  allow    app-Hyprland-chromium-f151403b.scope
xdg-terminal-exec         2  allow    app-Hyprland-xdg\x2dterminal\x2dexec-ccccc6e0.scope
udiskie                   1  allow    app-Hyprland-udiskie-317d8b51.scope
org.chromium.Chromium     1  allow    app-org.chromium.Chromium-2733.scope
```

### 5.7 A alternativa sem instalar nada: `virsh send-key`

Funciona, e foi testada aqui — três teclas no greeter deram três pontos na caixa
de senha:

```bash
virsh send-key omahouse-omarchy --codeset linux KEY_X KEY_Y KEY_Z
```

Mas ela manda **todas as teclas da chamada como um acorde simultâneo**, e não
uma sequência. Serve para `KEY_ENTER`, serve para `KEY_LEFTMETA KEY_ENTER`, e
não serve para digitar uma senha com letra repetida ou para escrever um comando.
Use quando a única coisa que falta é uma tecla e não vale instalar nada; use o
`ydotool` para todo o resto.

### 5.8 Olhar com o olho, em vez de dirigir

```bash
remmina -c vnc://127.0.0.1:5900
```

A tela sai por VNC em `127.0.0.1:5900` (`virsh vncdisplay omahouse-omarchy` diz
`127.0.0.1:0`, que é essa porta; `ss -ltn` confirma o `LISTEN`). No remmina,
marque **"Capturar todas as teclas"** — senão o `SUPER` vai para o Hyprland
*deste* computador em vez de ir para o da VM.

---

## 6. Capturar a tela — três ferramentas, três usos

| ferramenta | roda onde | pega | quando usar |
|---|---|---|---|
| `virsh screenshot` | no host | o framebuffer do qemu | greeter, tela de bloqueio, tela preta — **qualquer coisa**, com ou sem sessão |
| `grim` | dentro da sessão | o que o compositor desenhou | quando há sessão e se quer a imagem da janela |
| `mise run shots` | no host, sem VM | a janela do estúdio, offscreen | inventário de telas do `omahouse-studio` |

### 6.1 `virsh screenshot` — o framebuffer, sempre

```bash
virsh screenshot omahouse-omarchy /tmp/tela.png
```

```
Screenshot saved to /tmp/tela.png, with type of image/png
```

É a única que funciona **sem sessão**: pega o greeter do SDDM, pega a tela preta
que sobra depois de um `loginctl terminate-user`, pega o `hyprlock`. As imagens
`01`, `02`, `16`, `17`, `18` e `26` de `vm/shots/` são dela.

**O formato.** O libvirt diz na própria saída o que escreveu, e ele escreve o
que o qemu lhe deu — a extensão que você pediu não muda nada. Nesta máquina
(libvirt 12.6.0, qemu 11.1.0) saiu **PNG nas duas VMs**, inclusive quando o
arquivo foi pedido com `.ppm`:

```
$ virsh screenshot omahouse-poc /tmp/runbook-poc.ppm
Screenshot saved to /tmp/runbook-poc.ppm, with type of image/png
$ file /tmp/runbook-poc.ppm
/tmp/runbook-poc.ppm: PNG image data, 800 x 600, 8-bit/color RGB, non-interlaced
```

Então: **confira com `file`** antes de anexar em qualquer lugar, porque o nome
mente e a saída do `virsh` não. Se um dia sair PPM — que é o que versões mais
velhas do par libvirt/qemu devolvem — converta com o imagemagick que já está
aqui: `magick /tmp/tela.ppm /tmp/tela.png`. Esse `magick` **não foi exercitado
sobre um PPM de verdade nesta sessão**, porque nenhum apareceu.

### 6.2 `grim` — de dentro da sessão

Precisa de sessão gráfica de pé, e precisa rodar **como o dono dela**, com o
`XDG_RUNTIME_DIR` e o `WAYLAND_DISPLAY` dela:

```bash
g 'sudo ls /run/user/1001/ | grep wayland'      # descobrir qual é
# wayland-1

g 'sudo -u julia env XDG_RUNTIME_DIR=/run/user/1001 WAYLAND_DISPLAY=wayland-1 \
     grim /tmp/tela.png'

scp -i ~/.ssh/id_vms howl@192.168.122.60:/tmp/tela.png /tmp/tela.png
```

O `grim` escreve o arquivo como `julia`, com modo `0644`, então o `scp` como
`howl` alcança sem mais nada.

> **Medido, e contra o esperado.** Na `omahouse-omarchy` as duas ferramentas
> deram o **mesmo arquivo, byte por byte** — mesmo md5, `magick compare -metric
> AE` igual a `0`, 211879 bytes cada. O `bochs` é um framebuffer só: o
> `grim` lê o que o compositor desenhou nele e o `virsh` lê o mesmo bloco, e as
> duas codificam PNG com o mesmo libpng nos mesmos ajustes. A diferença entre
> elas aqui **não é qualidade de imagem, é alcance**: o `grim` precisa de sessão
> e o `virsh` não. Escolha por isso.

### 6.3 `mise run shots` — o estúdio, sem VM nenhuma

```bash
mise run shots           # escreve docs/img/
mise run shots:check     # confere que docs/img é o que a janela desenha hoje
```

Não precisa de tela, de sessão nem de VM: constrói o binário de teste do estúdio,
aponta as duas raízes para uma árvore temporária, semeia o exemplo com os verbos
do próprio `omahouse` e dirige a janela real sob eventos de tecla reais com
`QT_QPA_PLATFORM=offscreen`. São 24 quadros de 1100x700, e cada um é recusado se
for um retângulo chapado.

Para gerar sem sujar a árvore — que é o que se faz quando só se quer provar que
o gerador funciona — passe um destino:

```bash
.scripts/shots.sh /tmp/runbook-shots
```

```
01-operator-people.png  1100x700
02-operator-programs.png  1100x700
…
24-subject-nothing.png  1100x700
```

`shots:check` está dentro do `mise run verify`, então uma mudança na janela que
não venha com as imagens novas não passa do commit.

**As três não competem.** `docs/img/` é inventário e se regenera;
`vm/shots/` é o registro de uma tarde e **não** se regenera — nasceu do
`virsh screenshot` e do `grim` de uma corrida do roteiro e é a prova de que
aquilo aconteceu. `docs/screens.md` §2 diz quais telas são de qual.

---

## 7. Restaurar o estado depois

Quem roda um teste manual e não restaura deixa a VM mentindo para o próximo.
Faça isto na `omahouse-omarchy` **antes** de desligar.

O estado de referência dela, que o `vm/provision-omarchy.sh` deixa e a §4 do
`vm/OMARCHY-VM.md` descreve, é: perfil da `julia` com sessão de 10 min, Chromium
de 3 min, `default: deny`, `enforce: true`, `grace: 20`, `warnAt: [10, 5, 1]`;
**nenhum** arquivo de ledger; **nenhum** `/etc/omahouse/blocked`; e o `state.conf`
do SDDM apontando para `julia`.

```bash
# 1. o dia — apaga saldo gasto e concessões
g 'sudo rm -f /var/lib/omahouse/julia/$(date +%F).json'

# 2. o bloqueio de re-login
g 'sudo rm -f /etc/omahouse/blocked'

# 3. o perfil, se a §6 do OMARCHY-VM.md foi usada para afrouxar os limites
g 'sudo omahouse profile default julia --deny
   sudo omahouse profile enforce julia --on
   sudo omahouse limit julia --session 10m
   sudo omahouse limit julia --budget chromium=3m
   sudo omahouse limit julia --budget org.chromium.Chromium=3m'

# 4. o greeter volta a pedir a senha da julia
g 'sudo sed -i "s|^User=.*|User=julia|" /var/lib/sddm/state.conf
   sudo systemctl restart sddm'

# 5. o que foi instalado para o teste sai
g 'sudo systemctl stop ydotoold
   sudo systemctl reset-failed ydotoold
   sudo pacman -Rns --noconfirm ydotool
   sudo rm -f /run/ydotoold.socket'
```

Conferindo que ficou como devia:

```bash
g 'sudo cat /var/lib/sddm/state.conf
   sudo ls -la /var/lib/omahouse/julia/
   sudo cat /etc/omahouse/blocked 2>&1
   omahouse profile show julia
   systemctl is-active ydotoold 2>&1'
```

O esperado é `User=julia`, o diretório da `julia` **vazio**, `No such file or
directory` para o `blocked`, os limites da §4 no perfil, e `inactive` para o
`ydotoold`.

> **Trocar o greeter para `howl`.** É `sed` no `/var/lib/sddm/state.conf` mais
> `systemctl restart sddm`, como acima com `howl` no lugar de `julia`. O
> `vm/OMARCHY-VM.md` §3 manda usar `vm-login-as howl`, e **esse comando não
> existe nesta VM** — o `vm/provision-omarchy.sh` o escreve, mas o disco que
> está aí nunca o recebeu (`/usr/local/bin/` só tem o
> `omahouse-vm-greeter-guard`). Foi por isso que a corrida de aceitação editou
> o `state.conf` à mão, como diz a legenda de `26-greeter-howl.png`.
>
> E o greeter do Omarchy **não mostra para quem está pedindo a senha**: as
> capturas com `User=howl` e com `User=julia` são visualmente idênticas, uma
> caixa vazia. O `state.conf` é a única fonte de verdade.

Na `omahouse-poc` não há o que restaurar: o `vm/e2e.py` faz `reset()` entre cada
caso e mais uma vez no fim, apagando `profiles.json`, `blocked` e o
`/var/lib/omahouse` inteiro. É a máquina descartável de `testing.md` §6.

Por fim:

```bash
virsh shutdown omahouse-omarchy
virsh shutdown omahouse-poc
virsh list --all           # as duas em "shut off"
```

---

## 8. Reconstruir do zero

Se a `omahouse-omarchy` se perder:

```bash
vm/provision-omarchy.sh              # constrói, ou põe em dia
vm/provision-omarchy.sh --recreate   # joga o disco fora e começa de novo
```

**Uns 20 minutos.** Ele precisa, no host: libvirt sem `sudo`, `~/.ssh/id_vms`, e
o volume `omahouse-arch-base.qcow2` no pool `images`. Não instala nada no host.

O que ele faz: overlay de 40 G sobre a base, semente de cloud-init com as contas
`howl` e `root`, define o domínio com `--video model.type=bochs`, adiciona o
repositório `omarchy` e o keyring, instala os pacotes da sessão, roda o subconjunto
do `/usr/share/omarchy/install` que não é bootloader nem impressora, cria a
`julia`, aponta o SDDM para ela, instala o `omahouse-vm-greeter-guard`, constrói
o `omahouse` do `PKGBUILD` desta árvore e escreve o perfil da `julia`.

O que ele **deixa de fora**, e por quê: `limine` e seus hooks e o `snapper` (os
hooks reescreveriam o boot de uma imagem de nuvem que não boota assim);
`plymouth` (quer reconstruir o initramfs); e as ~150 aplicações do Omarchy que
não têm nada a dizer sobre escopo de app, `session.slice`, notificação ou
`logind` — docker, cups, bluez, obs, libreoffice e companhia. O que fica é a
sessão: `hyprland`, `uwsm`, `quickshell` (a barra **e** o daemon de notificação
**e** o agente de polkit), `sddm` com o greeter do Omarchy, os portais, `foot`
por `xdg-terminal-exec`, `chromium` e as fontes.

Ele também **não instala o `ydotool`** — isso é da §5, e é de propósito: a VM de
demonstração não carrega ferramenta de teste.

A `omahouse-poc` **não se reconstrói por script**. Ela é a semente da rodada 2 do
`poc/findings.md` e o próprio `e2e.py` diz o porquê quando não a encontra:
instalar de novo são vinte minutos de trabalho manual. Se ela sumir, é caso de
`blocked`, não de recriar às pressas.

Para descartar, cada uma tem sua receita: `vm/OMARCHY-VM.md` §8 para a de
demonstração, e a rodada 2 do `poc/findings.md` para a de teste.

---

## 9. As pegadinhas medidas

Custaram horas. Não podem ser redescobertas.

**Este qemu não tem `virtio-gpu` nem `qxl`.** O pacote instalado é o
`qemu-base` (headless), e o que ele oferece é isto e nada mais:

```
$ virsh domcapabilities --machine q35 --arch x86_64
      <enum name='modelType'>
        <value>vga</value>
        <value>cirrus</value>
        <value>vmvga</value>
        <value>none</value>
        <value>bochs</value>
        <value>ramfb</value>
      </enum>
```

**A `omahouse-omarchy` usa `bochs`.** Ele dá ao convidado um `/dev/dri/card0` do
driver `bochs-drm` — nativo, sem `modprobe` — o Hyprland faz modesetting atômico
nele por `kms_swrast`, e o VNC enxerga o que ele desenha. É o que torna a VM de
demonstração observável.

**A `omahouse-poc` usa `vkms`, e o VNC não a enxerga.** O módulo tem de ser
carregado **a cada boot** (`sudo modprobe vkms`; o `e2e.py` faz isso sozinho no
`wait_for_the_session`), e o que ele desenha não chega ao framebuffer `cirrus`
que o VNC lê. Um `virsh screenshot omahouse-poc` durante os casos devolve o
console de texto da tty1 — o que é útil, e não é a sessão:

```
Arch Linux 7.2.2-arch1-1 (tty1)
omahouse-poc login: julia (automatic login)
Authentication failure
```

(essa é a `julia` sendo recusada pelo `pam_listfile` durante o
`logout_bloqueia_o_reingresso` — a prova aparece por acaso na tela errada.)
Para testar serve; para **ver**, use a `omahouse-omarchy`.

**O Hyprland precisa de sessão de *seat* de verdade.** Por ssh ele não sobe.
Não adianta exportar `WAYLAND_DISPLAY` e mandar rodar: sem seat não há
`/dev/dri` e não há teclado. É exatamente por isso que a §5 existe, e por isso
que o autologin (poc) e o SDDM (omarchy) são parte do desenho e não conveniência.

**O `omahouse-vm-greeter-guard.service` existe porque o SDDM para.** O SDDM
0.21 lê uma sessão encerrada por `loginctl terminate-user` como
`ERROR_INTERNAL "Process crashed"` e não faz mais nada: nem greeter, nem display
novo. O guard reinicia o `sddm` quando o `seat0` fica sem sessão nenhuma por três
sondagens seguidas. São ~9 segundos de tela preta. **Ele não faz parte do
omahouse** — é remendo desta VM, e num Omarchy de fábrica `onExhausted: "logout"`
deixa a máquina numa tela preta.

**O `remmina` já está instalado neste host** (`1:1.4.43-3`) e é o cliente VNC a
usar. `virt-viewer` **não** está, e instalá-lo é uma decisão, não um passo.

**Corrida na subida da sessão.** O pid do Hyprland aparecer não quer dizer que o
socket Wayland já aceite conexão. Um cliente lançado nessa janela — o `mako` do
`e2e.py`, o `grim`, o que for — morre em silêncio. Espere um fato do lado de
dentro (`loginctl list-sessions`, `omahouse status`), não um pid. Foi isto que
deu o `BLOCKED` do primeiro run da §4.

**A `julia` começa a gastar tempo no login, sem tocar em nada.** O `udiskie` que
o próprio Omarchy sobe já é escopo de app vivo, e o relógio da sessão de 10
minutos corre a partir daí. Antes de qualquer trabalho manual demorado na VM,
dê folga — e lembre que a concessão vai para o ledger do dia:

```
$ g 'sudo omahouse grant julia --session 45m'
julia: +45m of session, from root. 54m left today.
```

Isso some com o `rm` do passo 1 da §7. Afrouxar por `omahouse limit` **não**
some, porque escreve no perfil — por isso o passo 3.

---

## 10. O que não foi verificado nesta sessão

Honestidade sobre o que este runbook não pode prometer:

- **`vm/provision-omarchy.sh` (§8) não foi executado.** São ~20 minutos e ele
  recria a VM que este documento precisava manter íntegra para o resto das
  provas. O que está na §8 é leitura do script mais o que os arquivos que ele
  escreve mostram no convidado (o `omahouse-vm-greeter-guard` está lá; o
  `vm-login-as` não está, e isso está dito na §7).
- **A conversão de PPM para PNG (§6.1) não foi exercitada**, porque o
  `virsh screenshot` deste host devolveu PNG nas duas VMs, nas duas placas de
  vídeo. O comando está lá para o caso de outra combinação libvirt/qemu.
- **`remmina -c vnc://127.0.0.1:5900` (§5.8) não foi aberto** — abrir uma janela
  no desktop de quem estava usando a máquina não é efeito colateral de escrever
  documentação. O que foi verificado é o que dá para verificar sem isso: o
  pacote instalado, `virsh vncdisplay` dizendo `127.0.0.1:0`, e o `LISTEN` em
  `127.0.0.1:5900`.
- **O roteiro de aceitação da §4 do `vm/OMARCHY-VM.md` não foi refeito inteiro.**
  Ele já está registrado quadro a quadro em `vm/shots/`. O que esta sessão
  refez foi cada *ferramenta* deste runbook, uma vez, para provar que ela
  funciona como está escrito.
