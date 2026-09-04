# Guia de uso do omahouse

Este guia leva alguém que acabou de instalar o omahouse até um perfil em pé:
uma conta com programas liberados, tempo por programa, tempo de sessão e as
regras valendo.

Tudo o que está aqui foi visto funcionar numa sessão Omarchy 4.0.2 de verdade.
As imagens são dessa corrida, e o que **não** funciona está na §6, com o nome
que tem. Um guia que ensina um caminho furado mente na primeira vez que alguém
o segue.

---

## 1. O que é

O omahouse guarda, para cada conta da máquina, um **perfil**: quais programas
ela pode abrir e por quanto tempo, por programa e no total do dia. Quem conta e
quem age é `omahouse watch`, um daemon root que acorda de dois em dois segundos,
lê os escopos de app da sessão, debita os orçamentos, avisa antes de acabar,
fecha o programa cujo tempo terminou e encerra a sessão cujo dia terminou. A
sessão da pessoa fiscalizada não decide nada — ela só recebe as notificações e
pode ler o próprio saldo.

Ler não pede privilégio: `omahouse status`, `omahouse report` e
`omahouse profile list|show` rodam para qualquer um. Escrever pede root. O
estúdio (`omahouse-studio`) nunca é root: toda escrita dele passa por
`pkexec omahouse …`, e o polkit pede a senha de um administrador.

---

## 2. Montar um perfil

### 2.1 Criar o perfil

```bash
sudo omahouse profile add julia --name "Júlia"
```

```
julia: profile written to /etc/omahouse/profiles.json
  observing — it counts and reports and closes nothing. Watch a day of
  `omahouse report julia`, then `omahouse profile enforce julia --on`.
  every app is allowed: `omahouse profile default julia --deny` turns the
  rules into a list of what is allowed instead.
```

O perfil nasce **sem dentes** (`enforce: false`) e **permitindo tudo**
(`default: allow`). Isso é de propósito: um perfil que conta sem morder dá um
dia de relatório antes de você ligar as regras, e um perfil escrito pela metade
que negasse tudo trancaria alguém para fora da própria máquina.

Uma conta no `wheel` é recusada, e essa é a única recusa dura do programa:

```
profile add: howl is in wheel, and an administrator does not fiscalise themselves by accident.
             Take the account out of wheel first, or write the profile for somebody else.
```

No estúdio: `n` na aba **people**, o campo pergunta `Which account?`, o polkit
pede a senha, e a recusa do `wheel` aparece no rodapé com essas mesmas palavras.

### 2.2 Descobrir o nome dos programas

A identidade de um app, para o omahouse, é o **id do escopo systemd** dele — não
o caminho do executável. `omahouse status` é o que lista esses ids:

```bash
omahouse status
```

```
APP                             PIDS  SCOPE
gtk-launch                        21  app-Hyprland-gtk\x2dlaunch-fa8e3f64.scope
xdg-terminal-exec                  4  app-Hyprland-xdg\x2dterminal\x2dexec-151e8e07.scope
org.chromium.Chromium              3  app-org.chromium.Chromium-2587456.scope
omarchy-hyprland-monitor-watch     2  app-Hyprland-omarchy\x2dhyprland\x2dmonitor\x2dwatch-f5a7f3ab.scope
udiskie                            1  app-Hyprland-udiskie-983fb161.scope
code                               1  app-code-3579042.scope
```

A coluna `APP` é o que se escreve num `allow`. O `status` também diz, num bloco
à parte, quando o nome mente:

```
Not what the name says
APP                             IS RUNNING                               PROCESSES  SCOPES
gtk-launch                      /usr/lib/chromium/chromium                19 of 21       1
code                            /usr/bin/dconf                            10 of 10      10
gtk-launch                      /usr/share/code/chrome_crashpad_handler     8 of 8       8
xdg-terminal-exec               /usr/bin/tmux                               2 of 4       1
udiskie                         /usr/bin/python3.14                         1 of 1       1
```

Leia esse bloco antes de escrever a lista. Ele é a diferença entre liberar um
programa e liberar um lançador.

### 2.3 O menu do Omarchy não serve para montar a allowlist

**Esta é a pegadinha maior, e ela decide o formato da sua lista.**

O menu do Omarchy (`SUPER + Espaço` → *Apps*) lança **tudo** por `gtk-launch`. O
escopo que nasce se chama `gtk-launch`, e não o nome do programa. Com
`default: deny`, o omahouse fecha na hora — e a notificação diz o nome do
lançador, não o do programa que a pessoa tentou abrir:

![O menu do Omarchy abriu "Regras da Casa"; dois segundos depois a notificação diz `gtk-launch is not allowed — It is not one of the programs released for Júlia.` A janela nunca apareceu.](../vm/shots/08-menu-gtk-launch-negado.png)

Todas as entradas do menu colapsam nesse id só. Liberar `gtk-launch` **libera o
menu inteiro**, e o conjunto do que ele lança muda a cada `.desktop` instalado.

Enquanto isso é assim, monte a lista a partir de programas abertos pelas
**teclas de atalho** do Omarchy, que dão ids reais:

| tecla | id que nasce |
|---|---|
| `SUPER + Enter` | `xdg-terminal-exec` |
| `SUPER + Shift + B` | `chromium` **e** `org.chromium.Chromium` |

E abra o próprio estúdio por linha de comando, de um terminal liberado:

```bash
uwsm app -- omahouse.desktop
```

### 2.4 Liberar um programa

```bash
sudo omahouse allow julia chromium --limit 3m
```

```
julia: chromium allowed, 3m a day, and it closes when the time is out.
```

`--limit` é açúcar: escreve a regra e o orçamento de uma vez. Um programa
liberado **sem** limite próprio roda e gasta só do total do dia.

Três coisas que precisam estar na lista e que não são óbvias:

- **O Chromium tem dois ids.** Uma janela produz `chromium` (os processos
  filhos) e `org.chromium.Chromium` (o dono da janela). Liberar só um fecha o
  navegador pelo motivo errado. Escreva os dois, e dê limite aos dois.
- **O Omarchy lança utilitários próprios como se fossem app.** O `autostart.lua`
  dele sobe `udiskie` e `omarchy-hyprland-monitor-watch` por `uwsm-app --`; sem
  regra, o omahouse os fecha dois segundos depois do login.
- **Não libere terminal num perfil que precisa segurar.** Um programa aberto de
  dentro do terminal herda o escopo do terminal: quem tem terminal liberado roda
  o que quiser, e tudo conta como tempo de terminal. Isto é o `spec.md` §10, e
  não tem conserto no motor.

A lista da `julia` da corrida ficou assim:

```bash
sudo omahouse allow julia xdg-terminal-exec
sudo omahouse allow julia omahouse
sudo omahouse allow julia chromium --limit 3m
sudo omahouse allow julia org.chromium.Chromium --limit 3m
sudo omahouse allow julia omarchy-hyprland-monitor-watch
sudo omahouse allow julia udiskie
```

No estúdio, `a` abre um seletor alimentado pelos `.desktop` instalados. Ele
**marca o shim**, que é o que impede liberar às cegas:

![O seletor "which program" do estúdio: "Basecamp" e "Discord" dizem em vermelho `that name is the launcher — inside it: omarchy-launch-webapp`, "Disk Usage" e "Docker" dizem `xdg-terminal-exec`, e "Chromium · chromium" aparece com `already listed`. Só "Foot · foot" e "btop++ · btop" trazem um id que é o próprio programa.](../vm/shots/34-studio-escolher-programa.png)

Depois do seletor vem `How long a day for foot?`, e o polkit. O rodapé confirma
com a mesma frase que o CLI imprime:
`julia: foot allowed, 5m a day, and it closes when the time is out.`

### 2.5 Dar o limite da sessão, e fechar a lista

```bash
sudo omahouse limit julia --session 2h
sudo omahouse profile default julia --deny
```

```
julia: session 2h a day, and it logs out when the time is out.
julia: only what is on the list runs. 6 rules on it.
```

Durações são `45m`, `2h`, `1h30m`, ou um número puro em minutos. Qualquer outra
coisa é recusada em vez de virar minutos.

No estúdio: `s` na aba **today** para o dia inteiro, `m` na aba **programs** para
o dia de um programa, `d` para trocar entre "só o que está na lista roda" e
"tudo roda menos o que está na lista".

### 2.6 Conferir, e só então ligar os dentes

```bash
omahouse profile show julia
```

```
omahouse profile — julia (Júlia)

SETTING  VALUE
enabled  yes
enforce  yes
default  deny
warn at  10m, 5m, 1m left
grace    20s

RULES
VERDICT  APP
allow    xdg-terminal-exec
allow    omahouse
allow    omarchy-hyprland-monitor-watch
allow    udiskie
allow    chromium
allow    org.chromium.Chromium

BUDGETS
BUDGET                 APP                    A DAY  WHEN OUT
chromium               chromium                  3m  closes
org.chromium.Chromium  org.chromium.Chromium     3m  closes
session                *                      2h00m  logs out
```

Antes de ligar, veja um ciclo sem consequência nenhuma:

```bash
omahouse watch --once --dry-run
```

Ele lê a árvore, debita o tique, imprime o que teria fechado e quem teria
recusado no próximo login, e não escreve nada, não notifica ninguém e não
encerra sessão alguma. Não pede privilégio.

Ligado:

```bash
sudo omahouse profile enforce julia --on
```

```
julia: enforcing — budgets now close and log out.
```

No estúdio, `e`.

---

## 3. O que a pessoa fiscalizada vê

**Não há indicador na barra.** O `quickshell` do Omarchy segue como veio; o
omahouse não põe nada nele. O que a pessoa vê são as notificações, e o que ela
pode consultar é `omahouse status` ou a janela do estúdio.

### O aviso

As marcas padrão são 10, 5 e 1 minuto restantes. A notificação diz o nome do
orçamento e a hora em que o corte acontece:

![Terminal à esquerda, Chromium à direita, e no canto superior direito a notificação `1 minute left / chromium closes at 08:34.` O relógio da barra marca 08:33.](../vm/shots/10-aviso-chromium-1min.png)

### A carência

Esgotado o orçamento, abre-se uma janela de 20 segundos entre o "acabou" e o
fechamento. Como o Chromium tem dois ids, vêm **duas** notificações, uma por id:

![Duas notificações empilhadas: `Time is up / org.chromium.Chromium closes in 20 seconds.` e `Time is up / chromium closes in 20 seconds.`](../vm/shots/11-aviso-chromium-carencia.png)

### O programa fecha, e a sessão fica

Vinte segundos depois, o Chromium sumiu. O terminal continua aberto, a barra
continua no lugar, a sessão continua de pé:

![A mesma tela sem o Chromium: só o terminal da `julia` maximizado, a barra intacta, relógio em 08:34.](../vm/shots/12-chromium-fechado.png)

Isto é o ponto todo do desenho: o `cgroup.kill` pega o escopo do app e a
`session.slice` nunca é tocada.

### O relógio da sessão segue correndo

O orçamento da sessão casa com tudo, então ele corre mesmo com a tela parada — o
`udiskie` que o próprio Omarchy sobe já é um escopo de app vivo desde o login:

![Notificação `5 minutes left / Your session ends at 08:40.` sobre o terminal, às 08:35.](../vm/shots/13-aviso-sessao-5min.png)

### O fim da sessão, e o login recusado

Quando o dia acaba, o nome vai para `/etc/omahouse/blocked`, o
`pam_listfile` do `/etc/pam.d/system-login` passa a recusar, e só então a sessão
é encerrada. A senha certa não entra mais: a caixa do greeter fica **vermelha** e
nada acontece.

![O greeter do Omarchy com a caixa de senha vermelha e o cadeado vermelho. Nenhuma mensagem explica o motivo.](../vm/shots/18-login-recusado.png)

O journal do outro lado diz `pam_listfile(sddm:account): Refused user julia`. O
greeter não diz nada — quem está na frente da máquina não descobre por ali que
foi o tempo que acabou.

### Ler o próprio saldo

A pessoa fiscalizada roda `omahouse status`, ou abre o estúdio, que na cara dela
é só leitura:

![O estúdio da `julia`, aba **programs**: `xdg-terminal-exec` (aberto agora, 2 processos, com o aviso vermelho `that name is not what is running: /usr/bin/bash — 1 of 2`), `House Rules · omahouse`, `Chromium · chromium` com `0m left of 3m` e a barra vermelha cheia, `org.chromium.Chromium` igual, `omarchy-hyprland-monitor-watch`, e `udiskie` com o mesmo aviso vermelho apontando `/usr/bin/python3.14`.](../vm/shots/23-studio-julia-programas.png)

O cabeçalho diz o que ela é: `julia · subject · under rules · writes through
pkexec`. O rodapé conta o que o omahouse não enxerga: `8 it cannot see`.

---

## 4. Operar no dia a dia

### Dar mais tempo agora, com o programa aberto

```bash
sudo omahouse grant julia --session 10m
sudo omahouse grant julia --budget chromium=15m
```

```
julia: +10m of session, from howl. 2h10m left today.
```

O tempo vai para o ledger do dia e expira com ele. Dois `grant` somam. Se o nome
estava em `/etc/omahouse/blocked`, ele sai sozinho no ciclo seguinte — em até
dois segundos a senha volta a entrar no greeter, sem que ninguém precise saber
que o arquivo existe.

No estúdio, `+`:

![O estúdio na aba **programs** da `julia`, com o diálogo "More time today for foot" aberto e o rodapé confirmando `julia: +10m of foot, from howl. 15m left today.` A linha do Foot mostra `15m left of 15m` e, embaixo, `+10m handed over today`.](../vm/shots/40-studio-tempo-extra-concedido.png)

> **Pegadinha do `+`.** A própria tecla `+` vaza para dentro do campo e o `ok`
> nasce apagado. Apague o `+` antes de digitar o número.
>
> ![O diálogo "More time today for foot" com um `+` solitário no campo e o botão `Enter ok` esmaecido.](../vm/shots/39-studio-mais-tempo-hoje.png)

> **Quem assina.** `grant` é assinado com o nome de quem pediu. Pelo estúdio,
> isso é a pessoa que o polkit autenticou — `howl handed over 10m of foot`. Por
> `sudo omahouse grant`, isso é `root`, e o relatório de um mês depois vai dizer
> `root handed over 45m of session`. Se o nome importa no seu registro, conceda
> pelo estúdio.

### Afrouxar os limites de vez

```bash
sudo omahouse limit julia --session 4h            # mais dia
sudo omahouse profile enforce julia --off         # conta e relata, não fecha
sudo omahouse profile default julia --allow       # tudo roda menos o negado
```

`enforce --off` é o modo observação, e é para onde voltar quando alguma coisa
está mordendo demais e você ainda não sabe o quê.

### Tirar um programa da lista, ou alguém dos livros

```bash
sudo omahouse deny julia chromium
sudo omahouse profile remove julia
```

`profile remove` tira o perfil e não encosta na conta nem no histórico: os dias
já contados ficam em `/var/lib/omahouse/julia/`, porque relatório é prova e
sobrevive à regra que o coletou. No estúdio, `x` faz as duas coisas conforme a
aba em que você está.

### Ler o dia

```bash
omahouse report julia
omahouse report julia --since 2026-09-01
```

```
omahouse report — julia

2026-09-04
  nothing counted

GRANTS
AT        BY    BUDGET   ADDED
09:01:50  howl  session   +10m
```

O mesmo dia, no estúdio, aba **today**:

![O estúdio da `julia` na aba **today**: no topo `the whole day · session · running now`, `12m spent`, `52m left of 1h5m` e `+55m handed over today`; abaixo `Chromium` e `org.chromium.Chromium`, ambos `3m spent · 0m left of 3m`; e a lista do dia em ordem inversa, das concessões (`root handed over 45m of session`) até os avisos das 08:30, com uma linha vermelha às 08:32: `app-Hyprland-gtk\x2dlaunch-c5075ae6.scope: not on the list, and was closed`.](../vm/shots/24-studio-julia-hoje.png)

Os eventos não são enfeite. É neles que uma decisão que só pode acontecer uma
vez lembra que já aconteceu — ler o dia é ler por que um aviso saiu ou não saiu.

---

## 5. O estúdio pelo teclado

```bash
uwsm app -- omahouse.desktop
```

O estúdio tem **duas caras**, e ninguém escolhe qual. Quem está no `wheel` recebe
a de operador; qualquer outro recebe a de sujeito. O cabeçalho diz qual é:
`howl · operator · is in wheel · writes through pkexec` contra
`julia · subject · under rules · writes through pkexec`.

Toda ação existe como **tecla e como botão**, porque há uma tabela só de comandos
na janela: os botões são desenhados dela, as teclas são procuradas nela, e `:` e
`?` a listam. Uma ação não consegue existir em só um dos dois.

`?` abre o mapa, e ele muda com o contexto — a seção **here, right now** só traz
o que a linha sob o cursor aceita agora:

![O mapa de teclas do operador: as colunas *move* (`j`/`k`, setas, `g`/`G`, `Home`/`End`, `PgDn`/`PgUp`), *go* (`l`/`Enter` abre o perfil, `h` volta, `Esc` volta ou limpa o filtro, `1`/`2`/`3` para people, programs e today) e *window* (`/` filtra, `:` comandos, `?` esta lista, `Tab` próximo controle, `Space` aciona o controle sob o teclado); e embaixo **here, right now**: `n` põe uma conta sob regras, `e` só observar, `d` deixa tudo rodar menos o listado, `x` tira a conta dos livros.](../vm/shots/29-studio-howl-teclas.png)

O mesmo `?` na sessão da pessoa fiscalizada não tem **nenhuma** tecla de escrita
— nem `n`, nem `a`, nem `+`, nem seção **here, right now**:

![O mapa de teclas da `julia`: só as colunas *move*, *go* e *window*. Nada abaixo delas.](../vm/shots/21-studio-teclas-julia.png)

A paleta (`:`) obedece à mesma tabela: na sessão da `julia` ela tem um comando
só, `back to the people`.

Resumo das teclas de escrita, todas passando por `pkexec`:

| tecla | o que faz |
|---|---|
| `n` | põe uma conta sob regras |
| `e` | liga e desliga os dentes (`enforce`) |
| `d` | allowlist ou denylist |
| `a` | libera um programa |
| `m` | minutos por dia daquele programa |
| `s` | quanto dura o dia inteiro |
| `+` | mais tempo **hoje** |
| `x` | tira o programa da lista, ou a conta dos livros |

> **Pegadinha de layout.** Em janela estreita, o cabeçalho sobrepõe o subtítulo e
> as abas, e o resultado é ilegível. Maximize a janela.

---

## 6. O que ainda não funciona

Seis coisas foram medidas quebradas num Omarchy de verdade. Nenhuma delas tem
conserto no omahouse hoje.

### 6.1 Programas abertos pelo menu não dão para liberar pelo nome

**O que acontece.** O menu do Omarchy (`SUPER + Espaço`) lança tudo por
`gtk-launch`. Todas as entradas colapsam num id só. Sob `default: deny`, o programa
é fechado antes de a janela aparecer, e a notificação acusa `gtk-launch`
(imagem da §2.3). No relatório do dia sobra a linha
`app-Hyprland-gtk\x2dlaunch-…: not on the list, and was closed`.

**O que fazer enquanto isso.** Monte a allowlist a partir das teclas de atalho do
Omarchy, que dão ids reais, e abra o que falta por
`uwsm app -- <nome>.desktop` de um terminal liberado. `sudo omahouse allow julia
gtk-launch` faz o menu funcionar — e libera o menu inteiro, que é um conjunto
desconhecido e variável de programas.

### 6.2 Se a tela travar por ociosidade, os últimos avisos passam despercebidos

**O que acontece.** O `hypridle` do Omarchy tranca a tela com o `hyprlock` e dois
segundos depois apaga o monitor. Os avisos de 1 minuto e de carência saem para
trás da tela de bloqueio. Quem sai da frente da máquina volta com a sessão já
encerrada, sem ter visto nada.

![A tela de bloqueio do `hyprlock`: papel de parede borrado e a caixa `Enter Password` no centro. Foi para trás disto que os dois últimos avisos da sessão saíram.](../vm/shots/15-hyprlock-ocultou-avisos.png)

**O que fazer enquanto isso.** Trate o aviso de 5 minutos como o último
confiável. Se o perfil precisa mesmo avisar até o fim, desligue o bloqueio por
ociosidade do Omarchy nessa conta — sabendo que isso é afrouxar outra coisa.

### 6.3 Avisos de orçamento curto queimam todos juntos

**O que acontece.** As marcas são 10, 5 e 1 minuto restantes. Num orçamento menor
que a maior marca, as marcas que já nasceram vencidas cruzam no mesmo instante e
disparam de uma vez. No Chromium de 3 minutos, as marcas de 10 e de 5 minutos
saíram juntas às 08:30, no momento em que o navegador abriu; só a de 1 minuto
caiu onde queria dizer alguma coisa, às 08:33. A sessão de 10 minutos disparou a
marca de 10 no próprio login. Isso está no relatório do dia, na imagem da §4.

**O que fazer enquanto isso.** Ou dê orçamentos maiores do que a maior marca, ou
edite `warnAt` à mão em `/etc/omahouse/profiles.json` — não há verbo de CLI para
esse campo.

### 6.4 Ao esgotar a sessão, a tela fica preta

**O que acontece.** O SDDM 0.21 lê uma sessão encerrada por
`loginctl terminate-user` como `Process crashed` e não faz mais nada: nem
greeter, nem display novo. A tela fica assim:

![Uma tela inteiramente preta. É o que o SDDM deixa depois de `loginctl terminate-user`.](../vm/shots/16-tela-preta-pos-logout.png)

Na VM de demonstração há um serviço de remendo (`omahouse-vm-greeter-guard`) que
reergue o SDDM quando o `seat0` fica sem sessão; ele levou 9 segundos. Esse
serviço **não** faz parte do omahouse. Num Omarchy de fábrica, não há nada que
traga o greeter de volta.

**O que fazer enquanto isso.** Instale um serviço equivalente, que reinicie o
`sddm` quando o assento fica sem sessão, ou não use `onExhausted: "logout"` numa
máquina onde ninguém vai poder chegar ao console.

### 6.5 O Omarchy lança utilitários próprios como se fossem app

**O que acontece.** O `autostart.lua` do Omarchy sobe `udiskie` e
`omarchy-hyprland-monitor-watch` por `uwsm-app --`. Eles nascem como escopo de
app, e sob `default: deny` sem regra o omahouse os fecha dois segundos depois do
login. Além disso, o `udiskie` sozinho é um escopo vivo: o orçamento da sessão,
que casa com tudo, **corre desde o login**, com a máquina parada e nenhuma janela
aberta.

**O que fazer enquanto isso.** Ponha os dois na allowlist de todo perfil, e conte
o dia sabendo que ele começa no login e não na primeira janela.

### 6.6 O Chromium aparece como dois ids

**O que acontece.** Uma janela do Chromium no Omarchy real produz dois escopos
com dois ids: `chromium`, com os processos filhos, e `org.chromium.Chromium`, com
o processo dono da janela. Liberar ou limitar só um fecha o navegador pelo motivo
errado, e a carência chega em duas notificações (imagem da §3).

**O que fazer enquanto isso.** Escreva os dois `allow` e os dois `--limit`, com o
mesmo número:

```bash
sudo omahouse allow julia chromium --limit 3m
sudo omahouse allow julia org.chromium.Chromium --limit 3m
```

### 6.7 Defeitos do estúdio

- A tecla `+` vaza para dentro do campo de "mais tempo hoje" e deixa o `ok`
  apagado. Apague o `+` antes de digitar.
- Em janela estreita, o cabeçalho sobrepõe o subtítulo e as abas. Maximize.

---

## O que o omahouse não é

**Não é fronteira de segurança.** A allowlist julga escopos de app. Um programa
lançado de dentro de um terminal herda o escopo do terminal, e um programa
lançado por um `exec` cru num keybinding cai dentro da unidade do compositor,
onde o omahouse não consegue nem contar nem fechar — `omahouse status` reporta
esse número em "Out of reach", e ele nunca é zero numa sessão viva.

A força da regra é propriedade de **quem é o operador**, não do motor. Um perfil
administrado por outra pessoa segura de verdade; um perfil que a pessoa impõe a
si mesma ela desfaz quando quiser, e tudo bem — é disciplina, não prisão.

O que o modelo segura, porque não depende da boa vontade da sessão: o relógio, o
encerramento por `loginctl`, a contagem (o ledger é escrito pelo root) e o
próprio daemon (`Restart=always`).

`spec.md` diz o porquê de cada uma dessas escolhas; `docs/cli.md` tem os verbos
inteiros; `vm/OMARCHY-VM.md` descreve a máquina onde as imagens deste guia foram
tiradas.
