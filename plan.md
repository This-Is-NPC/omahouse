# omahouse — plano de implementação

Spike com a lógica antes da interface. O `spec.md` diz o que é; este arquivo diz
em que ordem construir, o que cada etapa entrega e como saber que ela acabou.

Regra que atravessa tudo: **nada de fiscalização roda na máquina de
desenvolvimento.** Etapas 6 e 7 só na VM `omahouse-poc` (`poc/findings.md`).

---

## Etapa 1 — esqueleto

Molde do `omafiles`, sem nenhuma lógica.

```
omahouse.pro              TEMPLATE=subdirs, ordered, cli e studio dependem de core
qmake/layout.pri          shadow build obrigatório, OBJECTS_DIR e afins
qmake/version.pri         OMAHOUSE_VERSION, a única fonte da versão
mise.toml                 deps build test verify lint studio usage:gen
.scripts/*.sh             build deps test verify qml-check usage-gen
.githooks/pre-commit      exec .scripts/verify.sh
LICENSE README.md .gitignore
```

**Pronto quando:** `mise run build` produz `build/bin/omahouse` (um `main` que
imprime a versão) e `mise run verify` passa.

---

## Etapa 2 — `core`, tudo puro

O coração, e a única parte difícil de acertar. Nada aqui toca disco, `/proc`,
relógio ou rede.

### Tipos

```cpp
enum class Verdict { Allow, Deny };
enum class OnExhausted { Warn, Close, Logout };

struct Rule    { QString match; Verdict verdict; };
struct Budget  { QString id, match; int dailyMinutes; OnExhausted onExhausted; };
struct Profile { QString user, displayName;
                 bool enabled, enforce;
                 Verdict defaultVerdict;
                 QVector<int> warnAt;  int graceSeconds;
                 QVector<Rule> rules;  QVector<Budget> budgets; };

struct AppScope { QString id, unit, cgroupPath; int pidCount; };
struct Ledger   { QString user; QDate date;
                  QHash<QString,int> seconds;
                  QVector<Grant> grants; QVector<Event> events; };

struct Decision { enum Kind { Warn, Close, Logout } kind;
                  QString budgetId, scopeUnit; int secondsLeft; };
```

### A única porta de entrada

```cpp
struct Outcome { Ledger ledger; QVector<Decision> decisions; };

Outcome evaluate(const Profile&, const QVector<AppScope>&,
                 const Ledger&, const QDateTime& now, int tickSeconds);
```

`now` entra por parâmetro. É o que permite provar orçamento de duas horas em
microssegundos, e o que dispensa mexer no relógio do sistema nos testes.

### O parser do nome do escopo mora aqui

Puro, e com os casos reais colhidos na PoC:

| unidade | id |
|---|---|
| `app-Hyprland-chromium-031bdc27.scope` | `chromium` |
| `app-code-3579042.scope` | `code` |
| `app-flatpak-org.freedesktop.Platform-2351381583.scope` | `org.freedesktop.Platform` |
| `app-Hyprland-xdg\x2dterminal\x2dexec-151e8e07.scope` | `xdg-terminal-exec` |

O último exige des-escapar `\x2d`. É o tipo de detalhe que só aparece medindo.

### Testes

`tst_policy.cpp` — veredito padrão nos dois sentidos, regra vencendo o padrão,
orçamento esgotando, `warnAt` disparando uma vez só por marca, `grace`,
`enforce:false` não emitindo `Close` nem `Logout`, orçamento sem limite.

`tst_ledger.cpp` — débito uma vez por orçamento e não por processo, virada de
dia, `grant` somando, ida e volta do JSON, escrita atômica.

**Pronto quando:** os dois testes passam e `evaluate` não tem nenhuma chamada
de sistema.

---

## Etapa 3 — `Proc`

O único lugar que lê a máquina.

```cpp
class Proc {
public:
    QVector<AppScope> scopesFor(uid_t) const;        // app.slice
    int sessionSliceProcesses(uid_t) const;          // o ponto cego do §5
};
```

Lê `/sys/fs/cgroup/user.slice/user-<uid>.slice/user@<uid>.service/app.slice/`,
conta PIDs por `cgroup.procs`, e devolve escopos já com o id resolvido pelo
parser da etapa 2.

`sessionSliceProcesses` existe porque o `spec.md` §5 promete relatar o que não
consegue ver: app lançado por `exec` cru cai junto do compositor.

**Pronto quando:** rodando na máquina de desenvolvimento, lista os mesmos
escopos que `systemctl --user list-units` mostra.

---

## Etapa 4 — CLI de leitura

Sem privilégio, e é o primeiro retorno visível.

```
omahouse status [user]        escopos vivos, id, tempo de hoje, saldo
omahouse report <user>        o ledger do dia, ou de um intervalo
omahouse profile list|show
```

Sem perfil configurado, `status` roda em modo avulso: varre agora e mostra o
que existe. É o que valida etapa 2 e 3 contra uma máquina real, hoje.

`omahouse.usage.kdl` nasce aqui, e `docs/cli.md` sai dele por `mise run
usage:gen`. Um teste assere que a versão declarada bate com `--version`.

**Pronto quando:** `omahouse status $USER` na sua máquina lista Chromium, VS
Code e o terminal com os ids certos.

---

## Etapa 5 — CLI de escrita

Aqui a lógica fica completa, e revisável sem uma linha de UI.

```
omahouse profile add <user> [--name] [--create-user]
omahouse profile remove|enforce|default
omahouse allow <user> <id> [--limit 45m]
omahouse deny  <user> <id>
omahouse limit <user> --session 2h | --budget <id>=45m
omahouse grant <user> --session 10m | --budget <id>=15m
```

Grava `/etc/omahouse/profiles.json` com tmp + `rename`. Recusa perfil de
usuário no `wheel` — administrador não se fiscaliza por engano.

`tests/test_cli.py` faz o e2e em `$TMPDIR`, com a raiz de configuração
injetável por variável de ambiente para não precisar de root.

**Pronto quando:** cria, edita e lê um perfil de ponta a ponta, e `--create-user`
faz `useradd -m` fora do `wheel`.

---

## Etapa 6 — `watch` em observação

O laço, ainda sem dentes.

Ciclo de 2 s: `Proc::scopesFor` → `evaluate` → grava ledger → executa só as
`Decision` do tipo `Warn`. `enforce:false` é o padrão de perfil novo.

**Pronto quando:** roda um dia inteiro na VM com uso real e o `report` bate com
o que foi usado, sem nada ter sido fechado.

---

## Etapa 7 — os dentes

Cada mecanismo já foi medido na PoC; aqui é só ligar.

| ação | mecanismo | medido em |
|---|---|---|
| `Close` | `SIGTERM` no escopo, `grace`, `cgroup.kill` | rodada 2 |
| `Warn` | `systemd-run --uid=… --setenv=DBUS_SESSION_BUS_ADDRESS=… notify-send` | rodada 2 |
| `Logout` | escreve em `/etc/omahouse/blocked`, `terminate-user`, apaga na virada | rodada 3 |

Empacotamento junto: `omahouse.service` com `Restart=always`, a policy do
polkit, o `.install` criando `/etc/omahouse` e `/var/lib/omahouse`, e a linha
de `pam_listfile` com `onerr=succeed`.

**Pronto quando:** os casos de `testing.md` passam na caixa `nspawn`, e
`session_slice_intocada.py` passa na VM.

---

## Etapa 8 — `studio`

QML no molde do `omafiles`, e a regra é: **tudo pelo teclado, e o mouse faz o
mesmo.** Nenhuma ação existe só no clique.

### Teclas, herdadas do omafiles

```
j / k        move o cursor            / filtra a lista
l / Enter    entra, edita             : comandos
h / Esc      volta                    ? mapa de teclas
Tab          próximo controle         1 2 3 visões
```

### Duas caras, escolhidas por quem abriu

**Operador** (no `wheel`) — perfis, saldo do dia ao vivo, editor de allowlist
alimentado pelos `.desktop` instalados, e liberar tempo.

**Sujeito** (tem perfil) — leitura. Quanto sobrou hoje, por app e no total.

A tela fala de programas e minutos. `default:deny` com uma lista de `allow` se
apresenta como "programas liberados", nunca como formulário de vereditos.

### Estrutura

`Theme.cpp` lê o tema do omarchy e o arredondamento do `looknfeel.lua` ao vivo.
`QML_IMPORT_NAME` + `CONFIG += qmltypes` para o `qmllint` enxergar os tipos
registrados em C++. `mise run lint` com todo aviso fatal.

Escrita vai por `pkexec omahouse …`; o studio nunca é root.

**Pronto quando:** dá para criar um perfil, liberar dois apps com limite e ver
o saldo sem encostar no mouse — e depois repetir tudo só com o mouse.

---

## Fora do spike

Plugin da barra do `omarchy-shell`, janelas de horário, presets, tempo em foco,
relatório histórico com gráfico, e qualquer coisa em rede.

## Riscos conhecidos

**O ponto cego do `exec` cru** (`spec.md` §5) não tem solução, só relato. Se na
prática ele pegar apps demais, o modelo de identidade precisa de um segundo
sinal — e isso se descobre na etapa 6, não antes.

**`useradd` e PAM são irreversíveis o bastante** para nunca serem exercitados
fora da VM. A etapa 5 escreve o verbo; quem o roda de verdade é a etapa 7, na
caixa.
