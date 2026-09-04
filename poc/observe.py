#!/usr/bin/env python3
"""Observador: mostra o que o omahouse VERIA e DECIDIRIA, sem tocar em nada.

Sem root para o próprio usuário; com root para outro. Nunca envia sinal,
nunca escreve fora do arquivo de saída. É o `enforce: false` do spec.md
feito à mão, antes de existir uma linha de C++.

  ./observe.py                 # o próprio usuário, 12 s
  ./observe.py --user julia --seconds 60 --out baseline.json
"""
import argparse, collections, json, os, pwd, sys, time
from pathlib import Path

def scan(uid):
    """{exe: [pids]} para os processos vivos do uid. Ignora o que sumiu no meio."""
    out = collections.defaultdict(list)
    for entry in os.scandir("/proc"):
        if not entry.name.isdigit():
            continue
        try:
            if entry.stat().st_uid != uid:
                continue
            exe = os.readlink(f"/proc/{entry.name}/exe")
        except (OSError, PermissionError):
            continue
        out[exe].append(int(entry.name))
    return out

def classify(exe):
    """O que este caminho quebra no modelo de allowlist por exe."""
    if exe.endswith(" (deleted)"):
        return "apagado"          # binário substituído por atualização
    if "/newroot/" in exe or exe.startswith("/app/"):
        return "flatpak"          # a raiz não é a nossa
    if exe.endswith("/bwrap") or exe.endswith("/flatpak"):
        return "sandbox"          # todos os flatpaks colapsam neste caminho
    if not exe.startswith(("/usr/", "/opt/")):
        return "fora do sistema"  # ~/.local, /tmp: o furo do spec.md §10
    return ""

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--user", default=pwd.getpwuid(os.getuid()).pw_name)
    ap.add_argument("--seconds", type=int, default=12)
    ap.add_argument("--interval", type=float, default=2.0)
    ap.add_argument("--out")
    args = ap.parse_args()

    try:
        uid = pwd.getpwnam(args.user).pw_uid
    except KeyError:
        sys.exit(f"usuário desconhecido: {args.user}")
    if uid != os.getuid() and os.geteuid() != 0:
        sys.exit(f"para observar '{args.user}' rode como root")

    seconds = collections.Counter()   # exe -> segundos debitados
    peak = collections.Counter()      # exe -> maior nº de processos simultâneos
    ticks = 0
    deadline = time.monotonic() + args.seconds

    while time.monotonic() < deadline:
        live = scan(uid)
        for exe, pids in live.items():
            # a regra do spec.md §5: debita uma vez por seletor, não por processo
            seconds[exe] += args.interval
            peak[exe] = max(peak[exe], len(pids))
        ticks += 1
        time.sleep(args.interval)

    print(f"\n  {args.user}: {len(seconds)} executáveis distintos em {ticks} amostras\n")
    print(f"  {'SEG':>5} {'PROC':>5}  {'ALERTA':<14} EXECUTÁVEL")
    print(f"  {'-'*5} {'-'*5}  {'-'*14} {'-'*46}")
    for exe, secs in seconds.most_common():
        flag = classify(exe)
        print(f"  {secs:5.0f} {peak[exe]:5d}  {flag:<14} {exe}")

    problemas = {e: classify(e) for e in seconds if classify(e)}
    multi = {e: n for e, n in peak.items() if n > 3}
    print(f"\n  caminhos problemáticos para allowlist: {len(problemas)}")
    print(f"  executáveis com mais de 3 processos:   {len(multi)}")

    if args.out:
        Path(args.out).write_text(json.dumps({
            "user": args.user, "samples": ticks, "interval": args.interval,
            "seconds": dict(seconds), "peak": dict(peak), "problems": problemas,
        }, indent=2, sort_keys=True) + "\n")
        print(f"  gravado em {args.out}")

if __name__ == "__main__":
    main()
