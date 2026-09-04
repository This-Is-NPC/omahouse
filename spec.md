# omahouse — especificação

Regras da casa para as contas de uma máquina Omarchy: quais programas cada
perfil pode abrir, e por quanto tempo.

Isto não é um problema novo. É gerência de lan house: um operador libera tempo,
o terminal conta, avisa antes de acabar e encerra a sessão sozinho quando o
crédito zera. O modelo está validado há décadas. A adaptação é que os
"terminais" são contas de usuário na mesma máquina, e o "operador" é quem
administra a casa.

O motor não sabe o que é uma criança. Ele sabe o que é um perfil, uma regra e
um orçamento. Limitar o tempo de tela de uma filha e limitar o próprio tempo de
Twitter são a mesma configuração com números diferentes.

---

## 1. O mapa da lan house

| Lan house | omahouse |
|---|---|
| Terminal (o micro) | conta de usuário Linux, sem privilégio |
| Operador, o caixa | quem está no `wheel` |
| Ficha, crédito | orçamento de minutos do dia |
| Início da sessão | login |
| "Faltam 5 minutos" | notificação na sessão do perfil |
| Encerrar e liberar o micro | `loginctl terminate-user` |
| Programas liberados | allowlist por caminho de executável |
| Fechamento do caixa | relatório do dia |

Três coisas que a lan house resolveu e que valem ser copiadas ao pé da letra:

**Tudo é saldo, não proibição.** A lan house não pergunta "você pode jogar?",
pergunta "quantos minutos você comprou?". Isso colapsa allowlist e limite de
tempo num conceito só: um programa fora da lista é um programa com saldo zero.

**O terminal não confia em si mesmo.** Quem decide é o servidor; o micro só
exibe. Aqui: quem decide é um daemon root, e a sessão do perfil só mostra o que
sobrou. É por isso que a fiscalização **não** pode morar num plugin do
`omarchy-shell` — ele roda como o próprio usuário fiscalizado, que o mata com
um `pkill`.

**O operador libera tempo na hora, sem reiniciar nada.** Precisa funcionar com
a sessão aberta e o programa rodando.

---

## 2. O modelo

Três substantivos, e nenhum deles é específico de um caso de uso.

**Perfil** — uma conta de usuário sob regras. Tem um veredito padrão, uma lista
de regras e uma lista de orçamentos.

**Regra** — um par `(seletor, veredito)`. O seletor é a **identidade do app**
(veja §5); o veredito é `allow` ou `deny`. O veredito padrão do perfil decide o
que acontece com quem não casa com regra nenhuma: `deny` é allowlist, `allow` é
denylist.

**Orçamento** — um contador diário com seletor, limite e ação de esgotamento.

A generalização que faz o modelo caber em três substantivos é esta: **a sessão
é só o orçamento cujo seletor é `*`**. Não existe "tempo do usuário" e "tempo
do app" como conceitos separados — existe orçamento, e um deles casa com tudo.
No código não há caso especial para sessão.

```
Budget { id, match, dailyMinutes, onExhausted }

  onExhausted:  close    fecha os processos que casam com o seletor
                logout   encerra a sessão do usuário
                warn     só avisa e registra
```

`session` declara `match: "*"` e `onExhausted: "logout"`. Um jogo declara a
identidade dele e `onExhausted: "close"`. É a mesma estrutura.

**`logout` são duas coisas, não uma.** A PoC mediu: `loginctl terminate-user`
encerra a sessão em segundos, e num sistema com autologin o usuário volta no
instante seguinte. Encerrar sem impedir a volta é teatro.

O bloqueio é uma linha de PAM padrão, sem módulo próprio, em
`/etc/pam.d/system-login`:

```
account required pam_listfile.so item=user sense=deny \
        file=/etc/omahouse/blocked onerr=succeed
```

`onerr=succeed` não é opcional: arquivo ausente ou ilegível não pode trancar
ninguém para fora da própria máquina. Então `logout` é escrever o nome no
arquivo, `terminate-user`, e apagar na virada do dia — validado em VM, com o
autologin recusado e a sessão voltando sozinha ao desbloquear.

---

## 3. Arquitetura

O molde é o do `omafiles`: um core estático sem opinião, e frentes finas por
cima. Três `SUBDIRS`, `cli` e `studio` dependendo de `core`.

| projeto | binário | roda como |
|---|---|---|
| `src/core` | `libomahousecore.a` | — |
| `src/cli` | `omahouse` | usuário, e root no `watch` |
| `src/studio` | `omahouse-studio` | usuário, **nunca** root |

**O daemon é um subcomando, não um quarto projeto.** O systemd chama
`omahouse watch`. Sem segundo binário, sem IPC, sem duplicar a leitura dos
perfis — o `core` já sabe fazer isso.

**O studio nunca é root.** Toda escrita passa por `pkexec omahouse ...`, com
uma policy do polkit exigindo `auth_admin`. Leitura é livre.

```
omahouse/
├── omahouse.pro                   # 3 SUBDIRS, ordered
├── qmake/{layout,version}.pri
├── mise.toml                      # deps build studio test verify lint usage:gen
├── omahouse.usage.kdl             # -> docs/cli.md
├── src/
│   ├── core/                      # Profile Rule Budget Ledger Policy Proc Paths
│   ├── cli/main.cpp               # verbos + watch
│   └── studio/                    # Theme.cpp, ProfileModel, qml/
├── packaging/
│   ├── omahouse.service
│   ├── org.omarchy.omahouse.policy
│   ├── omahouse.desktop
│   └── omahouse.svg
├── tests/
└── docs/cli.md
```

---

## 4. Estado em disco

Dois arquivos, os dois JSON, os dois lidos e escritos pelo `core`.

### `/etc/omahouse/profiles.json` — root escreve, todos leem (0644)

```json
{
  "schemaVersion": 1,
  "profiles": [
    {
      "user": "julia",
      "displayName": "Júlia",
      "enabled": true,
      "enforce": true,
      "default": "deny",
      "warnAt": [10, 5, 1],
      "grace": 20,
      "rules": [
        { "match": "minecraft-launcher", "verdict": "allow" },
        { "match": "firefox",            "verdict": "allow" },
        { "match": "gcompris",           "verdict": "allow" }
      ],
      "budgets": [
        { "id": "session",   "match": "*",                   "dailyMinutes": 120, "onExhausted": "logout" },
        { "id": "minecraft", "match": "minecraft-launcher",  "dailyMinutes": 45,  "onExhausted": "close" },
        { "id": "firefox",   "match": "firefox",             "dailyMinutes": 60,  "onExhausted": "close" }
      ]
    }
  ]
}
```

- Um programa permitido sem orçamento próprio gasta só do `session`.
- `enforce: false` — **modo observação**: conta e relata, não fecha nem desloga.
- `warnAt` — minutos restantes em que o usuário é avisado.
- `grace` — segundos entre o aviso final e o `SIGKILL`.
- `default` ausente vale `allow`, não `deny`. Um perfil escrito pela metade que
  conta sem morder é recuperável; um que nega tudo tranca alguém para fora da
  própria máquina. É a mesma escolha do `onerr=succeed` do §2.
- `user` é o único campo obrigatório.

Os `events` do ledger não são só registro: são a **memória** do que já foi dito.
`warn` carrega a marca de `warnAt` que disparou (`minutes`), e é o que impede o
mesmo aviso de sair a cada dois segundos; `exhausted` carrega o instante que
abre a janela de `grace`, medido do disco e não de um contador em memória, para
que um daemon reiniciado no meio da janela a retome em vez de reabri-la.

O mesmo esquema, com `default: "allow"` e uma regra `deny` para cada distração,
é um perfil de foco para um adulto. Nada muda no motor.

### `/var/lib/omahouse/<user>/<AAAA-MM-DD>.json` — o ledger do dia (0644)

```json
{
  "schemaVersion": 1,
  "user": "julia",
  "date": "2026-09-03",
  "budgets": { "session": 4210, "minecraft": 2400, "firefox": 1830 },
  "grants": [
    { "at": "2026-09-03T19:12:04-03:00", "by": "howl", "budget": "session", "minutes": 10 }
  ],
  "events": [
    { "at": "2026-09-03T19:25:11-03:00", "kind": "warn",      "budget": "minecraft", "minutes": 5 },
    { "at": "2026-09-03T19:30:11-03:00", "kind": "exhausted", "budget": "minecraft" },
    { "at": "2026-09-03T19:31:02-03:00", "kind": "denied",    "scope": "app-Hyprland-steam-9f2c11ab.scope" }
  ]
}
```

Um arquivo por dia, escrito atomicamente (tmp + `rename`). O saldo reseta na
virada da data local; uma sessão em curso na virada continua aberta e passa a
debitar no arquivo novo.

---

## 5. A fiscalização

`omahouse watch`, root, ciclo de 2 segundos:

1. Descobre quais usuários com perfil têm sessão ativa (`/run/user/<uid>` existe).
2. Lista os **escopos de app** do usuário em
   `/sys/fs/cgroup/user.slice/user-<uid>.slice/user@<uid>.service/app.slice/`.
3. Extrai a identidade de cada escopo e casa com as regras do perfil, ou cai no
   veredito padrão.
4. Debita 2 s de cada orçamento com ao menos um escopo vivo casando com o
   seletor — uma vez por orçamento, não por processo.
5. Decide:
   - veredito `deny` → fecha na hora, notifica "esse programa não está liberado"
   - orçamento esgotado → aviso, `grace`, e a `onExhausted` dele
6. Persiste o ledger.

**Escopo vivo é escopo vivo, tenha nome ou não.** Um escopo com processos dentro
é uma pessoa usando a máquina, e o seletor `*` casa com todos eles — inclusive os
que o parser não consegue nomear, como os 46 `tmux-spawn-<uuid>.scope` desta
máquina. É o que faz o orçamento `session` medir tempo de máquina em vez de tempo
de app com nome bonito: sem isso, uma tarde inteira dentro do terminal debita
zero, e para um perfil de criança essa é a rota de fuga óbvia. O id só é exigido
onde faz falta de verdade — casar um orçamento ou uma regra **específicos** —, de
modo que um escopo sem id nunca é debitado por um orçamento de um app só e nunca
é julgado por uma regra nominal: ele cai no **veredito padrão** do perfil. A
consequência é intencional: com `default: deny` e `enforce: true`, um escopo sem
id é fechado, o que é a leitura coerente de uma allowlist — algo que não se
consegue sequer nomear certamente não está na lista de liberados. O `enforce:
false` de um perfil novo é o que protege durante a calibragem, e o `status` diz
essas duas coisas na cara: esses escopos entram no total, e não dá para dar
limite próprio a eles nem liberá-los por nome.

### A identidade de um app é o cgroup, não o executável

Este era o desenho anterior — casar `/proc/<pid>/exe` contra caminhos de
binário — e a PoC o derrubou. `poc/findings.md` tem os números; o resumo é que
uma sessão normal tem 57 executáveis distintos, o Chromium sozinho responde por
21 processos com o mesmo `exe`, e `/usr/bin/bash` aparece 26 vezes sendo ora
infraestrutura, ora o terminal que o usuário abriu. Caminho de binário não
identifica um app, não agrupa a árvore dele e não separa app de encanamento.

O `uwsm` do Omarchy já resolveu isso. Todo app lançado vai para um escopo
próprio sob `app.slice`, e o encanamento da sessão fica em `session.slice`:

```
session.slice/wayland-wm@hyprland.desktop.service   Hyprland, quickshell
session.slice/pipewire.service                      pipewire
app.slice/app-graphical.slice/app-Hyprland-chromium-031bdc27.scope
app.slice/app-code-3579042.scope
```

Três consequências, e as três simplificam:

**A baseline deixa de existir como lista.** Não há `baseline.json` para manter,
nem 45 nomes que mudam a cada atualização do Omarchy. A regra é estrutural:
`session.slice` nunca é tocada. Só `app.slice` está sob julgamento.

**A árvore do app vem agrupada de graça.** Os 21 processos do Chromium são um
escopo só. O `bash` que roda dentro do terminal pertence ao escopo do terminal,
que é a resposta certa — ele não é um app à parte.

**Fechar vira uma escrita, não uma caçada a PIDs.** `echo 1 >
<escopo>/cgroup.kill` encerra o cgroup inteiro de uma vez, sem ordem de
reaping, sem processo órfão, sem aba de crash. O `grace` continua: `SIGTERM` no
escopo, espera, e só então o `cgroup.kill`. Verificado em VM: um escopo de app
foi a zero PIDs enquanto o Hyprland seguiu intacto.

### O ponto cego: quem não passa pelo `uwsm-app`

O escopo só existe se o app foi lançado por `uwsm app`. A PoC mediu os dois
caminhos com o mesmo `sleep 600`:

| lançamento | cgroup |
|---|---|
| `uwsm app --` | `app.slice/…/app-Hyprland-sleep-7865852f.scope` |
| `hyprctl dispatch exec` | `session.slice/wayland-wm@hyprland.desktop.service` |

O segundo cai dentro da unidade do compositor: invisível para contabilizar e
intocável para fechar, porque o `cgroup.kill` levaria a sessão junto.

O Omarchy embrulha tudo em `uwsm-app --`
(`/usr/share/omarchy/default/hypr/helpers.lua:109` e os `omarchy-launch-*`),
então os apps dele têm escopo. O ponto cego é o `exec` cru escrito à mão num
keybinding, e o que nasce de dentro de um terminal.

O omahouse **relata o que não consegue ver** em vez de fingir que não existe:
um perfil com processo demais em `session.slice` rende um aviso no `status`.

### O shim apaga o nome do app

Segundo furo, medido na etapa 4 (`poc/findings.md`, rodada 4): app lançado por
um shim herda o nome do shim. Sete escopos `app-Hyprland-gtk\x2dlaunch-*` desta
máquina contêm, na verdade, VS Code; o terminal aparece como
`xdg-terminal-exec`.

Liberar `gtk-launch` numa allowlist é liberar um conjunto desconhecido de
programas.

O sinal que corrige é o `exe` dominante dos processos dentro do escopo — e
repare que os dois sinais erram em situações opostas: o id falha no shim e
acerta no flatpak; o `exe` falha no flatpak (`bwrap`) e acerta no shim.

`evaluate` continua casando por **id** e continua puro. O `exe` dominante é
informação de **configuração**: serve para o operador não liberar às cegas, e
para o `status` mostrar o que um escopo realmente contém. Não é critério de
decisão.

`enforce: false` continua sendo o padrão de um perfil recém-criado — não mais
para calibrar uma baseline, mas porque ver um dia de relatório antes de ligar
os dentes é o que a lan house sempre fez.

Sem seccomp, sem eBPF, sem AppArmor: é `QTimer`, leitura de `/sys/fs/cgroup` e
uma escrita.

### Onde a lógica mora

`Policy` e `Ledger` são funções puras: recebem a lista de processos, o perfil,
o ledger e `now`, e devolvem as decisões e o ledger novo. Nada de `/proc`, nada
de `kill()`, nada de relógio lá dentro — o mundo entra por parâmetro. É o que
torna a parte difícil testável sem root, do mesmo jeito que o `Store` do
`omafiles`.

`Proc` é o adaptador que lê `/sys/fs/cgroup` e `/proc` de verdade. O `watch` é
o laço que junta os dois e executa as decisões.

---

## 6. Avisos e encerramento

O daemon é root e o usuário está noutra sessão. Para falar com ele:

```
systemd-run --uid=<uid> --setenv=DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/<uid>/bus \
  notify-send "Faltam 5 minutos" "Minecraft fecha às 19:35"
```

Duas linhas de `QProcess`, sem processo auxiliar na sessão fiscalizada. Um
widget na barra mostrando o tempo restante é um plugin do `omarchy-shell` que
lê o ledger — bonito, e opcional: se o usuário matar o shell, ele perde o
aviso, não a regra.

Ninguém é cortado a seco. A sequência é sempre aviso → `warnAt` → `grace` → fim.

---

## 7. Superfície do CLI

```
omahouse profile add <user> [--name "Júlia"] [--create-user]
omahouse profile list
omahouse profile show <user>
omahouse profile remove <user> [--keep-account]
omahouse profile enforce <user> --on|--off
omahouse profile default <user> --allow|--deny

omahouse allow <user> <exec> [--limit 45m]
omahouse deny  <user> <exec>
omahouse limit <user> --session 2h
omahouse limit <user> --budget minecraft=45m

omahouse grant <user> --session 10m
omahouse grant <user> --budget minecraft=15m

omahouse status [user]
omahouse report <user> [--since 2026-09-01]
omahouse watch
```

`allow ... --limit` é açúcar: cria a regra e o orçamento de uma vez, porque é
como se pensa na hora de configurar.

Escrita exige root, e o studio chega lá por `pkexec`. `status` e `report` são
livres — o próprio usuário fiscalizado roda `omahouse status` e vê o que sobrou.

`--create-user` faz o `useradd -m`, fora do `wheel`, e nada mais.

Os verbos são declarados em `omahouse.usage.kdl` e o `docs/cli.md` é gerado
dele, como no `omafiles`.

> `grant` é o único verbo que fui além dos quatro itens do escopo acordado
> (usuários, allowlist, tempo por app, tempo por usuário). Está aqui porque um
> operador que não consegue liberar dez minutos com o jogo aberto não é um
> operador. Se for escopo demais para a v1, é o primeiro a sair.

---

## 8. O studio

Uma janela Qt Quick, duas caras, escolhidas pelo usuário que a abriu:

**Operador** (quem está no `wheel`): a lista de perfis, os saldos do dia ao
vivo, o editor de allowlist com os programas vindos dos `.desktop` instalados,
e o botão de liberar tempo.

**Sujeito** (quem tem perfil): só leitura. Quanto sobrou hoje, no total e por
programa.

**A tela não é um editor genérico de regras.** O motor é genérico; a interface
fala de programas e de minutos, que é como as pessoas pensam. `default: deny`
com uma lista de `allow` se apresenta como "programas liberados", e não como um
formulário de vereditos.

`Theme.cpp` lê o tema do omarchy e o arredondamento do `looknfeel.lua` ao vivo,
igual ao `omafiles`.

---

## 9. Empacotamento

PKGBUILD em `omarchy-pkgs/pkgbuilds/omahouse`, `"source": "local"`, construído
do tarball de uma tag. O caminho está escrito em
`../publicar-app-no-omarchy-pkgs.md`.

Instala os dois binários, o `.desktop`, o ícone, a policy do polkit e o
`omahouse.service`. Um `.install` cria `/etc/omahouse` e
`/var/lib/omahouse` e habilita o serviço.

```ini
[Unit]
Description=omahouse session accounting
After=systemd-logind.service

[Service]
ExecStart=/usr/bin/omahouse watch
Restart=always
RestartSec=2

[Install]
WantedBy=multi-user.target
```

`Restart=always` importa: um daemon parado é uma regra desligada, e uma conta
sem privilégio não para serviço de sistema.

---

## 10. O que isto é, e o que não é

**Não é fronteira de segurança.**

A allowlist julga escopos de app. Um programa lançado de dentro de um terminal
herda o escopo do terminal — então quem tem terminal liberado roda o que
quiser, e isso conta como tempo de terminal. A PoC também achou três binários
sob `~/.local/` rodando numa sessão comum, o que confirma que o furo não é
teórico. Mitiga-se não liberando terminal no perfil, e o README precisa dizer
isso em vez de escondê-lo.

A força da regra é uma propriedade de **quem é o operador**, não do motor. Um
perfil administrado por outra pessoa segura de verdade. Um perfil que a pessoa
impõe a si mesma ela desfaz quando quiser — e tudo bem, é disciplina, não
prisão.

O que o modelo **segura** bem, porque não depende da boa vontade da sessão:
o relógio (mudar a hora exige polkit `auth_admin`), o encerramento por
`loginctl`, a contagem (o ledger é escrito pelo root), e o daemon
(`Restart=always`).

---

## 11. Ordem de implementação

1. **`core`** — `Profile`, `Rule`, `Budget`, `Ledger`, `Policy`, com
   `tst_policy.cpp` e `tst_ledger.cpp`. Sem tocar em `/proc`, sem root. É aqui
   que a modelagem trava e o resto vira consequência.
2. **`cli`** — os verbos, `omahouse.usage.kdl`, `test_cli.py`.
3. **`Proc` + `watch`** — a leitura de `app.slice` de verdade, o `cgroup.kill`,
   o `enforce: false` como padrão.
4. **`packaging`** — service, policy, `.install`. **Aqui já funciona de ponta a
   ponta pelo terminal: este é o MVP.**
5. **`studio`** — a cara do operador por cima do que já funciona.
6. Plugin do `omarchy-shell` com o tempo restante na barra.

O marco que importa é o 4. Parando ali, as regras da casa já valem; o studio é
a cara, não o motor.

---

## 12. Fora do escopo da v1

O escopo da v1 são quatro coisas: perfis de usuário, allowlist de programas,
tempo por programa e tempo por usuário. O resto espera, e o modelo de §2
comporta tudo isto sem mudança estrutural quando a hora chegar.

- **Janelas de horário** — "só das 15h às 20h", com `pam_time.so`. O bloqueio
  de re-login que o `logout` exige (§2) é primo deste e entra na v1; a janela
  por horário do dia continua fora.
- **Presets** — `kids`, `focus`, `kiosk` como sementes prontas em
  `/usr/share/omahouse/presets/`. Enquanto não existirem, o README carrega os
  exemplos.
- **Encerrar por ociosidade** e limpar a conta no logout (o caso quiosque).
- **Tempo em foco** em vez de tempo de execução. Mais justo, exige IPC do
  Hyprland na sessão fiscalizada, e ela pode matar.
- **Filtro de sites**, DNS, proxy — outro problema, outro app.
- **Vários micros em rede.** O modelo aguenta, a v1 é uma máquina só.
- **Crédito que atravessa dias**, banco de horas, tempo comprado com tarefa.
