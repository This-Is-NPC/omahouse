# O teste de aceitação manual, em imagens

Corrida completa do roteiro do `vm/OMARCHY-VM.md` na VM `omahouse-omarchy`,
em 2026-09-04, entre 08:29 e 08:54. O desktop foi dirigido por teclado
(`ydotool` sobre `uinput`, falando com o `seat0` da VM) e a tela capturada
por `virsh screenshot` (framebuffer do qemu, funciona no greeter e na tela
preta) e por `grim` de dentro da sessão (imagem melhor, só dentro da sessão).

Todos os limites eram os do guia — sessão 10m, Chromium 3m, `enforce: true`,
`default: deny` — até a imagem 19. Da 19 em diante a sessão da `julia` ganhou
tempo extra (`omahouse grant julia --session 45m`) para caber o passeio pelo
`omahouse-studio`. A VM foi devolvida ao estado do guia no fim.

## Entrar e usar

| imagem | o que mostra |
|---|---|
| `01-greeter.png` | O greeter do Omarchy no SDDM, uma caixa de senha e nada mais. |
| `02-greeter-senha-digitada.png` | Cinco pontos: o `ydotool` chega ao greeter. |
| `03-julia-sessao.png` | A sessão da `julia` de pé: barra do `quickshell`, papel de parede. Nenhum aviso na tela — os três primeiros avisos já tinham saído aos 08:30:29, antes de existir daemon de notificação. |
| `04-terminal.png` | `SUPER+Enter`. O escopo nasce `xdg-terminal-exec`. |
| `05-chromium.png` | `SUPER+Shift+B`. Dois escopos, `chromium` e `org.chromium.Chromium`. O "Restaurar páginas?" é herança de um `cgroup.kill` anterior. |

## O menu (`SUPER+Espaço`) — problema 1

| imagem | o que mostra |
|---|---|
| `06-menu-super-espaco.png` | O menu do Omarchy aberto. |
| `07-menu-apps-regras-da-casa.png` | *Apps* → "Regras da Casa", com o ícone certo. |
| `08-menu-gtk-launch-negado.png` | Dois segundos depois: **`gtk-launch is not allowed — It is not one of the programs released for Júlia.`** A notificação apareceu; a janela do estúdio, não. |
| `09-menu-nada-abriu.png` | Seis segundos depois: nada. O escopo `app-Hyprland-gtk\x2dlaunch-c5075ae6.scope` levou SIGTERM às 08:32:05. |

## O Chromium esgotando os 3 minutos

| imagem | o que mostra |
|---|---|
| `10-aviso-chromium-1min.png` | 08:33:04 — `1 minute left · chromium closes at 08:34.` |
| `11-aviso-chromium-carencia.png` | 08:34:06 — os dois `Time is up … closes in 20 seconds`, um por id. |
| `12-chromium-fechado.png` | 08:34:26 — o Chromium sumiu; terminal, barra e sessão intactos. |
| `13-aviso-sessao-5min.png` | 08:35:32 — `5 minutes left · Your session ends at 08:40.` |
| `14-grim-sessao.png` | A mesma tela por `grim`, de dentro da sessão. |

## A sessão esgotando os 10 minutos

| imagem | o que mostra |
|---|---|
| `15-hyprlock-ocultou-avisos.png` | 08:39:24 — o `hypridle` do Omarchy **trancou a tela** por inatividade, e dois segundos depois apagou. Os dois últimos avisos da sessão (1 min e carência) saíram para uma tela preta e trancada. Ninguém previu isto. |
| `16-tela-preta-pos-logout.png` | 08:40:50 — `loginctl terminate-user` feito, SDDM parado em `Process crashed`, tela preta. |
| `17-greeter-de-volta.png` | 08:40:58 — o `omahouse-vm-greeter-guard` reiniciou o SDDM. Nove segundos de preto. |
| `18-login-recusado.png` | A senha da `julia` de novo: a caixa fica **vermelha** e nada acontece. `pam_listfile(sddm:account): Refused user julia`. O greeter não diz por quê. |
| `19-julia-de-volta.png` | Depois de `omahouse grant julia --session 10m`, a mesma senha entra. |

## O `omahouse-studio` como `julia` (cara de leitura)

| imagem | o que mostra |
|---|---|
| `20-studio-julia.png` | Aberto por `uwsm app -- omahouse.desktop`. Em janela estreita o cabeçalho **sobrepõe** o subtítulo e as abas — defeito de layout. |
| `21-studio-teclas-julia.png` | O mapa de teclas (`?`) da `julia`: só mover, navegar e filtrar. Nenhuma tecla de escrita. |
| `22-studio-julia-pessoas.png` | Em tela cheia o cabeçalho fica legível: `julia · subject · under rules · writes through pkexec`. |
| `23-studio-julia-programas.png` | Os seis programas dela, os dois Chromium zerados, e o aviso vermelho `that name is not what is running` para `xdg-terminal-exec` e `udiskie`. |
| `24-studio-julia-hoje.png` | O dia inteiro em ordem: os avisos, os fechamentos, e em vermelho `app-Hyprland-gtk\x2dlaunch-…: not on the list, and was closed`. |
| `25-studio-julia-paleta.png` | A paleta (`:`) da `julia` tem um comando só: "back to the people". |

## O `omahouse-studio` como `howl` (cara de operador)

| imagem | o que mostra |
|---|---|
| `26-greeter-howl.png` | O greeter agora oferece `howl` (o guia manda usar `vm-login-as`, que **não existe** na VM; troquei `/var/lib/sddm/state.conf` à mão). |
| `27-sessao-howl.png` | A sessão do `howl`, com os avisos de primeira execução do Omarchy que ficam grudados no canto. |
| `28-studio-howl-pessoas.png` | `howl · operator · is in wheel · writes through pkexec`, e a fileira de escrita: `n` novo perfil, `e` só observar, `d` allow/deny, `x` fora dos livros. |
| `29-studio-howl-teclas.png` | O mesmo `?`, agora com a seção "here, right now" — o mapa muda com o contexto. |
| `30-studio-novo-perfil.png` | `n` → "Which account?", já avisando que conta no `wheel` é recusada. |
| `31-studio-polkit-novo-perfil.png` | O polkit do Omarchy pede a senha, em português: "É preciso autenticar para mudar quais programas uma conta pode abrir…". Rodapé: `put howl under rules — waiting for polkit`. |
| `32-studio-recusa-wheel.png` | Autenticado, a recusa vem do CLI e aparece no rodapé: `Take the account out of wheel first, or write the profile for somebody else.` |
| `33-studio-howl-programas.png` | Os programas da `julia` com as teclas de escrita: `a` liberar, `m` minutos, `+` mais hoje, `x` fora da lista. |
| `34-studio-escolher-programa.png` | O seletor de programa lê os `.desktop` **e marca o shim**: "that name is the launcher — inside it: `omarchy-launch-webapp`" / "`xdg-terminal-exec`". |
| `35-studio-escolher-foot.png` | Filtrando por "Foot". |
| `36-studio-limite-do-programa.png` | "How long a day for foot?" |
| `37-studio-polkit-senha.png` | O polkit de novo, para a escrita. |
| `38-studio-foot-liberado.png` | `julia: foot allowed, 5m a day, and it closes when the time is out.` |
| `39-studio-mais-tempo-hoje.png` | `+` abre "More time today for foot" — e o próprio `+` **vaza para dentro do campo**, deixando o "ok" apagado. |
| `40-studio-tempo-extra-concedido.png` | `julia: +10m of foot, from howl. 15m left today.` |
| `41-studio-howl-hoje.png` | O dia da `julia` visto pelo operador, com as concessões no topo. |
| `42-studio-duracao-do-dia.png` | `s` → "How long is julia's day?", já preenchido com `10m`. |
| `43-studio-dia-definido.png` | `julia: session 10m a day, and it logs out when the time is out.` |
