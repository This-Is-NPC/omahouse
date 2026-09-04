# omahouse — estratégia de testes

O omahouse é difícil de testar pela mesma razão que é difícil de furar: ele vive
no `systemd-logind`, no PAM, no polkit e no `/proc` de um usuário que não é o
que roda o teste. Nada disso existe num teste unitário, e quase nada existe num
container Docker.

A saída é três camadas, cada uma provando o que a de baixo não alcança, e a
mais cara rodando o menos possível.

---

## 1. As três camadas

| | camada | onde roda | tempo | roda quando |
|---|---|---|---|---|
| 0 | unidade | processo local | ms | todo commit |
| 1 | caixa | `systemd-nspawn` | ~15 s | todo commit |
| 2 | ponta a ponta | QEMU com Omarchy real | ~3 min | manual e noturno |

### O que cada camada prova, e o que ela não alcança

**Camada 0 — `Policy` e `Ledger`.** São funções puras por decisão de projeto
(`spec.md` §4): recebem processos, perfil, ledger e `now`, devolvem decisões e
o ledger novo. Sem `/proc`, sem `kill()`, sem relógio. Toda a aritmética de
saldo, virada de dia, `warnAt`, `grace` e crédito concedido se prova aqui, em
milissegundos, com `now` injetado.

Não alcança: se o `kill` mata mesmo, se o logind desloga, se o polkit autoriza.

**Camada 1 — a caixa.** Um `systemd-nspawn --boot`: systemd de verdade como
PID 1, `logind` de verdade, `dbus` de verdade, PAM, polkit, `useradd`, e um
namespace de PID próprio — então o `/proc` que o daemon varre é exatamente o
do container, sem ruído da máquina hospedeira.

Prova: allowlist matando processo, débito no ledger, `grace`, o unit do
systemd, o `.install` do pacote, a policy do polkit, e `loginctl
terminate-user` encerrando uma sessão real.

Não alcança: Hyprland, `omarchy-shell`, SDDM, `notify-send` numa sessão Wayland
de verdade, o studio, e — o mais importante — **a separação entre
`session.slice` e `app.slice` numa sessão gráfica real**.

**Camada 2 — a VM.** Omarchy 4.0.1 instalado de verdade, SDDM, Hyprland,
`uwsm`, `quickshell`. O usuário sob perfil loga no greeter e a sessão dele é
uma sessão gráfica de verdade.

Prova: o motor não toca no compositor, a notificação aparece na tela, o
deslogamento por tempo esgotado acontece com a sessão gráfica aberta, e o
operador libera tempo pela rede sem reiniciar nada.

### Docker não serve aqui

Está instalado nesta máquina, mas é a ferramenta errada para este app: sem
systemd como PID 1, não há `logind`, não há sessão, não há `loginctl
terminate-user`, e o barramento de sistema não existe. Metade do omahouse é
justamente essa metade. `systemd-nspawn` dá tudo isso e já está instalado.

---

## 2. Camada 1 — a caixa

### O rootfs

Um Arch mínimo com os pacotes do `omarchy-base.packages` que importam
(`systemd`, `polkit`, `pam`, `shadow`, `dbus-broker`) mais o `omahouse`
construído do PKGBUILD do momento.

`container/rootfs.sh` faz `pacstrap` num diretório e empacota o resultado num
tarball **endereçado por conteúdo**: o nome é o sha256 da lista de pacotes mais
o do PKGBUILD. Segunda chamada com as mesmas entradas é um no-op verificado. É
o mesmo padrão do `vm/prepared.py` do omashiki, encolhido.

```
~/.cache/omahouse/box/<fingerprint>.tar.zst
```

### O ciclo

```bash
systemd-nspawn --boot --quiet \
  --directory=/var/lib/omahouse-test/<run_id>/rootfs \
  --machine=omahouse-<run_id> \
  --private-network \
  --bind-ro=<pacote-recém-construído>:/pkg
```

Sessão de verdade para o usuário sob perfil:

```bash
machinectl shell julia@omahouse-<run_id> /usr/local/bin/fake-minecraft
```

`machinectl shell` registra a sessão no `logind` do container. É o que faz
`loginctl terminate-user` ter o que encerrar.

### Os apps de mentira

A allowlist casa por caminho de executável, então o teste não precisa do
Minecraft. `fake-minecraft`, `fake-firefox` e `fake-desconhecido` são três
scripts de uma linha que dormem. Comportamento idêntico, do ponto de vista do
`Proc`: um PID, com um `/proc/<pid>/exe` resolvível, pertencendo ao UID certo.

### Os orçamentos são pequenos, o relógio é real

Ninguém espera duas horas para provar um limite de duas horas, e adiantar o
relógio de um sistema com systemd é receita para teste instável.

Os perfis de teste usam segundos: sessão de 40 s, app de 15 s,
`warnAt: [1]`, `grace: 3`. Relógio real, segundos reais, números pequenos. A
aritmética de horas e de virada de dia já foi provada na camada 0, com `now`
injetado — aqui só se prova que o mecanismo dispara.

Virada de dia na camada 1 é um caso à parte: escreve-se um ledger com a data de
ontem, sobe-se o daemon, e assere-se que o saldo de hoje começou zerado.

### Os casos

```
container/cases/
├── veredito_deny_fecha_desconhecido.py
├── veredito_allow_poupa_permitido.py
├── budget_esgota_avisa_e_fecha.py
├── budget_session_esgota_e_desloga.py
├── grant_estende_com_processo_vivo.py
├── enforce_off_conta_sem_matar.py
├── ledger_sobrevive_a_reboot_do_daemon.py
├── ledger_vira_o_dia.py
├── polkit_recusa_escrita_de_nao_admin.py
└── pacote_instala_e_habilita_o_servico.py
```

Cada caso recebe uma caixa recém-criada e a descarta ao terminar. Sem estado
compartilhado entre casos, sem ordem significativa.

### Custa root

`systemd-nspawn` e `pacstrap` exigem root. `mise run test:box` vai pedir sudo
uma vez, e é bom que isso esteja escrito antes de alguém descobrir sozinho. Em
CI, onde já se roda como root, o custo é zero.

---

## 3. Camada 2 — a VM

### A base preparada

Não existe cloud image do Omarchy, e instalar da ISO leva quase uma hora — o
`~/vms/create-omarchy-vms.sh` diz, com todas as letras, que a instalação é
manual e interativa. Fazer isso a cada run está fora de questão.

O padrão da casa já resolve: **base preparada, endereçada por conteúdo**. É o
que o `~/vms/base/omashiki-e2e/<sha256>.qcow2` é.

Instala-se o Omarchy uma vez, à mão, na VM `omahouse-prep`. Depois
`vm/prepare.py`:

1. cria a conta do operador e a `julia`, sem privilégio, fora do `wheel`;
2. configura o SDDM com autologin em `julia`;
3. instala a chave `~/.ssh/id_vms` para a conta do operador;
4. habilita o `sshd`;
5. achata o qcow2, calcula o sha256, publica em
   `~/vms/base/omahouse-e2e/<sha>.qcow2` e grava o ponteiro em
   `~/.cache/omahouse/vm-e2e/prepared.json`.

A partir daí cada run cria um overlay qcow2 apontando para essa base. Boot em
segundos, descarte em segundos.

O `omahouse` em si **não** entra na base — ele é construído do working tree e
instalado no convidado a cada run. É o artefato sob teste; congelá-lo na base
seria testar a versão errada.

Preparar de novo só quando o `prepare.py` ou a versão do Omarchy mudarem. A
base é lida, nunca escrita.

### O autologin é o que torna o teste determinista

Com o SDDM entrando direto em `julia`, o boot da VM *é* o "o usuário sentou no
computador". Não há greeter para automatizar, nem senha para digitar, e a
sessão que sobe é uma sessão gráfica de verdade, registrada no `logind`, com
Hyprland no DRM do virtio-gpu.

O operador entra por ssh, na conta dele, como um administrador entraria.

### Domínio próprio, descartável

Seguindo o guia de orquestração do omashiki: nome próprio (`omahouse-e2e`),
marcador de ownership no metadata do domínio, e a verificação de que o backing
path é exatamente o da base validada. Um domínio com o mesmo nome e definição
diferente é `blocked`, não é reaproveitado.

`test1`, `test2`, `omarchy1`, `omarchy2` e a base do omashiki ficam intocados.

### O caso que justifica a camada inteira

Não é o do tempo esgotado — esse a caixa já prova. É este:

**`session_slice_intocada.py`** — perfil com `enforce: true` e allowlist vazia.
Assere que, sessenta segundos depois, o Hyprland, o `quickshell`, o `pipewire`
e o `systemd --user` continuam vivos e a sessão de `julia` continua ativa em
`loginctl`.

O `spec.md` §5 diz que isso é estrutural: só `app.slice` é julgada. Este caso é
quem prova que a estrutura é essa mesmo numa sessão Omarchy de verdade, e não
só na máquina de quem escreveu.

Esse é o modo de falhar que mata o produto: allowlist estrita derrubando o
compositor dois segundos depois do login, e o usuário olhando para um greeter
sem entender por quê. Só uma sessão gráfica de verdade prova que não acontece.

### Os outros casos da VM

```
vm/cases/
├── session_slice_intocada.py
├── aviso_aparece_na_tela.py          # grim + assere pixels não-vazios no canto
├── sessao_esgota_com_hyprland_aberto.py
├── operador_libera_tempo_por_ssh.py
├── app_real_morre_e_hyprland_sobrevive.py
└── studio_abre_como_sujeito_em_leitura.py
```

`aviso_aparece_na_tela.py` tira um `grim` de dentro da sessão fiscalizada e
puxa por ssh. É o único jeito de provar que a notificação chegou onde o usuário
a veria.

---

## 4. A árvore

```
omahouse/
├── tests/                     # camada 0
│   ├── tst_policy.cpp
│   ├── tst_ledger.cpp
│   └── test_cli.py
├── container/                 # camada 1
│   ├── manifest.toml
│   ├── rootfs.sh
│   ├── box.py                 # ciclo de vida do nspawn
│   ├── fixtures/              # perfis de teste, apps de mentira
│   ├── cases/
│   └── run.sh
└── vm/                        # camada 2
    ├── manifest.toml
    ├── prepare.py
    ├── prepared.py
    ├── e2e.py
    ├── cases/
    └── run.sh
```

`prepared.py` é código já escrito e já revisado no omashiki. Copiar em vez de
reescrever, ajustando só as chaves do manifesto.

---

## 5. Os alvos do mise

```toml
[tasks.test]              # camada 0
[tasks."test:box"]        # camada 1  (pede sudo)
[tasks."test:vm"]         # camada 2
[tasks."test:vm:prepare"] # base preparada, fora do SLA
[tasks.verify]            # docs + qml-check + test + test:box
```

`verify` é o portão do commit e para na camada 1. A camada 2 é manual e
noturna: ela existe para pegar o que só aparece com um compositor na tela, e
esse tipo de regressão não chega uma vez por hora.

---

## 6. Regras que valem para as três

Do guia de orquestração do omashiki, e valem aqui sem mudança:

- Todo run recebe um `run_id` único, e toda mutação é registrada antes de ser
  desfeita.
- Recurso de teste é descartável por padrão. Manter uma VM ou uma caixa de pé
  exige uma flag daquela execução, nunca uma configuração persistida.
- Pré-requisito ausente é `blocked` explícito, não teste pulado em silêncio.
- Nunca se remove uma asserção para o teste passar.
- O relatório sai depois da limpeza, não antes.

E uma específica deste app:

- **Nenhum caso roda contra a máquina do desenvolvedor.** O omahouse mata
  processos e desloga usuários. Um teste que confunda o host com o convidado
  não dá um vermelho — dá um logout no meio do trabalho. O `box.py` e o
  `e2e.py` recusam-se a rodar se o alvo não for um namespace ou um domínio de
  propriedade comprovada, e essa checagem vem antes de qualquer outra coisa.

---

## 7. Ordem de construção

1. Camada 0 junto com o `core` — não é fase separada, é o passo 1 do `spec.md`.
2. `container/rootfs.sh` + `box.py` + os dois primeiros casos de allowlist.
   Aqui o daemon ganha um lugar seguro para existir.
3. O resto dos casos da caixa, acompanhando os passos 3 e 4 do `spec.md`.
4. `vm/prepare.py` e a base preparada — uma tarde de instalação manual, uma vez.
5. `session_slice_intocada.py` primeiro, os outros casos da VM depois.

O passo 2 é o que muda o dia a dia: sem ele, testar o daemon significa testá-lo
na própria máquina, que é exatamente o que a última regra da §6 proíbe.
