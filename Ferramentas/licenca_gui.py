#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Transcriber Live — Gerador de Licenças (interface grafica).

Uso do DESENVOLVEDOR. Precisa estar na mesma pasta que licenca.py e da
chave privada (transcriberlive-private.key).

Abrir: duplo clique em "Gerador de Licencas.command"
   ou: python3 licenca_gui.py
"""

import csv
import os
import re
import subprocess
import sys
import tkinter as tk
from datetime import datetime
from tkinter import filedialog, messagebox, ttk

AQUI = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, AQUI)

try:
    import licenca as core
except Exception as exc:                                    # pragma: no cover
    root = tk.Tk(); root.withdraw()
    messagebox.showerror("Transcriber Live",
                         "Não encontrei licenca.py nesta pasta.\n\n"
                         "Deixe licenca_gui.py e licenca.py juntos.\n\nDetalhe: %s" % exc)
    sys.exit(1)

# ---------------------------------------------------------------- cores (tema do produto)
BG      = "#0e1114"
PANEL   = "#161a1f"
WIDGET  = "#1f252c"
OUTLINE = "#2b333c"
TEXT    = "#e8ebee"
DIM     = "#8a95a3"
VERDE   = "#3ddc84"
AZUL    = "#4fc3f7"
ALERTA  = "#ffb84d"
ERRO    = "#ff5252"

PASTA_LICENCAS = os.path.join(AQUI, "licencas")
CSV_HISTORICO  = os.path.join(AQUI, "licencas-emitidas.csv")


def normaliza_id(txt):
    """Aceita 'tl 1147 89df 849c', '114789DF849C', 'TL-1147-89DF-849C'... -> TL-XXXX-XXXX-XXXX"""
    h = re.sub(r"[^0-9A-Fa-f]", "", re.sub(r"^\s*[Tt][Ll]", "", txt.strip()))
    h = h.upper()
    if len(h) != 12:
        return None
    return "TL-%s-%s-%s" % (h[0:4], h[4:8], h[8:12])


def abrir_no_finder(caminho):
    try:
        if sys.platform == "darwin":
            subprocess.run(["open", "-R" if os.path.isfile(caminho) else "", caminho],
                           check=False)
        elif os.name == "nt":
            os.startfile(os.path.dirname(caminho) if os.path.isfile(caminho) else caminho)
        else:
            subprocess.run(["xdg-open", os.path.dirname(caminho)], check=False)
    except Exception:
        pass


# ==============================================================================
class App(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("Transcriber Live — Gerador de Licenças")
        self.configure(bg=BG)
        self.geometry("820x780")
        self.minsize(780, 700)

        self.priv_path = tk.StringVar(value=os.path.join(AQUI, core.PRIV_FILE))
        self.priv = None

        self._estilo()
        self._cabecalho()

        nb = ttk.Notebook(self)
        nb.pack(fill="both", expand=True, padx=14, pady=(6, 12))
        self.aba_emitir = ttk.Frame(nb, style="Card.TFrame")
        self.aba_hist   = ttk.Frame(nb, style="Card.TFrame")
        self.aba_conf   = ttk.Frame(nb, style="Card.TFrame")
        nb.add(self.aba_emitir, text="  Emitir licença  ")
        nb.add(self.aba_hist,   text="  Emitidas  ")
        nb.add(self.aba_conf,   text="  Verificar  ")

        self._monta_emitir()
        self._monta_historico()
        self._monta_verificar()

        self._carrega_chave()
        self.bind("<Return>", lambda e: self.gerar())

    # -------------------------------------------------------------- aparencia
    def _estilo(self):
        st = ttk.Style(self)
        try:
            st.theme_use("clam")
        except tk.TclError:
            pass

        st.configure(".", background=BG, foreground=TEXT, fieldbackground=WIDGET,
                     bordercolor=OUTLINE, lightcolor=PANEL, darkcolor=PANEL,
                     focuscolor=DIM, troughcolor=WIDGET)
        st.configure("TFrame", background=BG)
        st.configure("Card.TFrame", background=PANEL)
        st.configure("TLabel", background=PANEL, foreground=TEXT, font=("Helvetica", 12))
        st.configure("Dim.TLabel", background=PANEL, foreground=DIM, font=("Helvetica", 11))
        st.configure("Head.TLabel", background=PANEL, foreground=TEXT,
                     font=("Helvetica", 13, "bold"))
        st.configure("Bg.TLabel", background=BG, foreground=DIM, font=("Helvetica", 11))
        st.configure("TEntry", fieldbackground=WIDGET, foreground=TEXT,
                     insertcolor=TEXT, bordercolor=OUTLINE, padding=5)
        st.configure("TButton", background=WIDGET, foreground=TEXT, bordercolor=OUTLINE,
                     focusthickness=0, padding=(12, 6), font=("Helvetica", 12))
        st.map("TButton", background=[("active", "#3a4450"), ("pressed", "#3a4450")])
        st.configure("TCheckbutton", background=PANEL, foreground=TEXT,
                     font=("Helvetica", 12), focuscolor=PANEL,
                     indicatorbackground=WIDGET, indicatorforeground=VERDE)
        st.map("TCheckbutton", background=[("active", PANEL)],
               indicatorbackground=[("selected", VERDE), ("active", "#3a4450")])
        st.configure("TNotebook", background=BG, bordercolor=OUTLINE, tabmargins=(2, 4, 2, 0))
        st.configure("TNotebook.Tab", background=BG, foreground=DIM, padding=(14, 7),
                     font=("Helvetica", 12))
        st.map("TNotebook.Tab", background=[("selected", PANEL)],
               foreground=[("selected", TEXT)])
        st.configure("Treeview", background=WIDGET, fieldbackground=WIDGET, foreground=TEXT,
                     bordercolor=OUTLINE, rowheight=24)
        st.configure("Treeview.Heading", background=PANEL, foreground=DIM,
                     font=("Helvetica", 11, "bold"))
        st.map("Treeview", background=[("selected", "#3a4450")])

    def _cabecalho(self):
        top = tk.Frame(self, bg=BG, height=66)
        top.pack(fill="x", padx=14, pady=(12, 0))
        top.pack_propagate(False)

        esq = tk.Frame(top, bg=BG)
        esq.pack(side="left", fill="y")
        tk.Label(esq, text="GERADOR DE LICENÇAS", bg=BG, fg=TEXT,
                 font=("Helvetica", 15, "bold")).pack(anchor="w", pady=(6, 0))
        self.lbl_chave = tk.Label(esq, text="", bg=BG, fg=DIM, font=("Helvetica", 11))
        self.lbl_chave.pack(anchor="w")

        cv = tk.Canvas(top, width=262, height=52, bg=BG, highlightthickness=0)
        cv.pack(side="right", pady=6)
        self._desenha_logo(cv)

    def _desenha_logo(self, cv):
        """Marca do produto desenhada no canvas (sem depender de imagem)."""
        # balao
        cv.create_rectangle(6, 4, 62, 40, fill="#f4f6f8", outline="", width=0)
        cv.create_polygon(12, 38, 12, 50, 26, 39, fill="#f4f6f8", outline="")
        # ondas (verde -> azul)
        alturas = [(14, 10), (21, 6), (28, 3), (35, 8), (42, 5), (49, 11)]
        for i, (x, top_off) in enumerate(alturas):
            t = i / (len(alturas) - 1.0)
            r = int(0x3d + (0x4f - 0x3d) * t)
            g = int(0xdc + (0xc3 - 0xdc) * t)
            b = int(0x84 + (0xf7 - 0x84) * t)
            cv.create_line(x, 6 + top_off, x, 38 - top_off,
                           fill="#%02x%02x%02x" % (r, g, b), width=4, capstyle="round")
        # texto
        cv.create_text(74, 17, text="TRANSCRIBER", anchor="w", fill=TEXT,
                       font=("Helvetica", 16, "bold"))
        cv.create_text(76, 36, text="L I V E", anchor="w", fill=DIM,
                       font=("Helvetica", 11))

    # -------------------------------------------------------------- aba emitir
    def _monta_emitir(self):
        f = self.aba_emitir
        for i in range(2):
            f.columnconfigure(i, weight=0)
        f.columnconfigure(1, weight=1)

        lin = 0
        ttk.Label(f, text="Dados do cliente", style="Head.TLabel").grid(
            row=lin, column=0, columnspan=3, sticky="w", padx=16, pady=(14, 2)); lin += 1

        self.e_nome  = self._campo(f, lin, "Nome completo", "Joao da Silva"); lin += 1
        self.e_email = self._campo(f, lin, "E-mail", "joao@exemplo.com"); lin += 1

        ttk.Label(f, text="ID da máquina", style="TLabel").grid(
            row=lin, column=0, sticky="w", padx=(16, 10), pady=5)
        wrap = ttk.Frame(f, style="Card.TFrame")
        wrap.grid(row=lin, column=1, columnspan=2, sticky="ew", padx=(0, 16), pady=5)
        self.e_mid = ttk.Entry(wrap, font=("Menlo", 13))
        self.e_mid.pack(side="left", fill="x", expand=True)
        self.e_mid.bind("<KeyRelease>", lambda e: self._valida_mid())
        self.lbl_mid = ttk.Label(wrap, text="", style="Dim.TLabel", width=22)
        self.lbl_mid.pack(side="left", padx=(10, 0))
        lin += 1
        ttk.Label(f, text="o cliente copia esse ID no painel LICENÇA do plugin (TL-XXXX-XXXX-XXXX)",
                  style="Dim.TLabel").grid(row=lin, column=1, columnspan=2, sticky="w",
                                           padx=(0, 16), pady=(0, 6)); lin += 1

        self.e_nota = self._campo(f, lin, "Nota (opcional)", "Pedido 1234"); lin += 1

        # validade
        box = ttk.Frame(f, style="Card.TFrame")
        box.grid(row=lin, column=0, columnspan=3, sticky="w", padx=16, pady=(8, 4)); lin += 1
        self.temp = tk.BooleanVar(value=False)
        ttk.Checkbutton(box, text="Licença temporária (aluguel / festival):", variable=self.temp,
                        command=self._toggle_dias).pack(side="left")
        self.e_dias = ttk.Entry(box, width=6, font=("Helvetica", 12))
        self.e_dias.insert(0, "30")
        self.e_dias.pack(side="left", padx=8)
        self.lbl_dias = ttk.Label(box, text="dias   (desmarcado = perpétua)", style="Dim.TLabel")
        self.lbl_dias.pack(side="left")
        self._toggle_dias()

        # botao gerar
        self.btn_gerar = tk.Button(f, text="GERAR LICENÇA", command=self.gerar,
                                   bg=VERDE, fg="#0e1114", activebackground="#5ae79a",
                                   activeforeground="#0e1114", relief="flat",
                                   font=("Helvetica", 14, "bold"), padx=18, pady=9,
                                   highlightthickness=0, cursor="hand2")
        self.btn_gerar.grid(row=lin, column=0, columnspan=3, sticky="w", padx=16, pady=(10, 6))
        lin += 1

        self.lbl_status = ttk.Label(f, text="", style="Dim.TLabel", wraplength=700,
                                    justify="left")
        self.lbl_status.grid(row=lin, column=0, columnspan=3, sticky="w", padx=16); lin += 1

        # resultado
        ttk.Label(f, text="Licença gerada", style="Head.TLabel").grid(
            row=lin, column=0, columnspan=3, sticky="w", padx=16, pady=(10, 2)); lin += 1

        self.txt = tk.Text(f, height=8, bg=WIDGET, fg=TEXT, insertbackground=TEXT,
                           relief="flat", font=("Menlo", 10), wrap="none",
                           highlightthickness=1, highlightbackground=OUTLINE)
        self.txt.grid(row=lin, column=0, columnspan=3, sticky="nsew", padx=16)
        f.rowconfigure(lin, weight=1); lin += 1

        acoes = ttk.Frame(f, style="Card.TFrame")
        acoes.grid(row=lin, column=0, columnspan=3, sticky="w", padx=16, pady=10)
        ttk.Button(acoes, text="Copiar licença", command=self.copiar).pack(side="left")
        ttk.Button(acoes, text="Salvar .txt", command=self.salvar).pack(side="left", padx=8)
        ttk.Button(acoes, text="Abrir pasta", command=lambda: abrir_no_finder(PASTA_LICENCAS)).pack(side="left")
        ttk.Button(acoes, text="Limpar campos", command=self.limpar).pack(side="left", padx=8)

    def _campo(self, f, lin, rotulo, placeholder):
        ttk.Label(f, text=rotulo, style="TLabel").grid(
            row=lin, column=0, sticky="w", padx=(16, 10), pady=5)
        e = ttk.Entry(f, font=("Helvetica", 12))
        e.grid(row=lin, column=1, columnspan=2, sticky="ew", padx=(0, 16), pady=5)
        return e

    def _toggle_dias(self):
        estado = "normal" if self.temp.get() else "disabled"
        self.e_dias.configure(state=estado)

    def _valida_mid(self):
        mid = normaliza_id(self.e_mid.get())
        if not self.e_mid.get().strip():
            self.lbl_mid.configure(text="", foreground=DIM)
        elif mid:
            self.lbl_mid.configure(text="ok  " + mid, foreground=VERDE)
        else:
            self.lbl_mid.configure(text="12 dígitos hex", foreground=ALERTA)
        return mid

    # -------------------------------------------------------------- chave
    def _carrega_chave(self):
        caminho = self.priv_path.get()
        if os.path.exists(caminho):
            try:
                self.priv = core.carregar_priv(caminho)
                bits = self.priv["n"].bit_length()
                self.lbl_chave.configure(
                    text="chave privada: %s  (%d bits)" % (os.path.basename(caminho), bits),
                    fg=DIM)
                self.btn_gerar.configure(state="normal", bg=VERDE)
                return
            except Exception as exc:
                self.lbl_chave.configure(text="chave ilegível: %s" % exc, fg=ERRO)
        else:
            self.lbl_chave.configure(text="chave privada não encontrada nesta pasta", fg=ALERTA)

        self.priv = None
        self.btn_gerar.configure(state="disabled", bg=WIDGET)
        self.after(300, self._oferece_chave)

    def _oferece_chave(self):
        if self.priv is not None:
            return
        r = messagebox.askquestion(
            "Chave privada",
            "Não encontrei %s nesta pasta.\n\n"
            "SIM  = escolher o arquivo da chave\n"
            "NAO  = criar um par de chaves novo\n\n"
            "Atenção: criar um par novo invalida todas as licenças já vendidas."
            % core.PRIV_FILE, icon="warning")
        if r == "yes":
            p = filedialog.askopenfilename(title="Escolha a chave privada",
                                           initialdir=AQUI,
                                           filetypes=[("Chave", "*.key"), ("Todos", "*.*")])
            if p:
                self.priv_path.set(p)
                self._carrega_chave()
        else:
            if messagebox.askokcancel("Criar par de chaves",
                                      "Vou gerar um par novo de 2048 bits nesta pasta.\n\n"
                                      "Isso invalida licenças emitidas com uma chave anterior.\n"
                                      "Continuar?"):
                self.lbl_chave.configure(text="gerando par de chaves...", fg=DIM)
                self.update_idletasks()
                priv = core.gerar_par()
                core.salvar_priv(priv, os.path.join(AQUI, core.PRIV_FILE))
                with open(os.path.join(AQUI, core.PUB_FILE), "w") as fh:
                    fh.write(core.linha_publica(priv) + "\n")
                self.priv_path.set(os.path.join(AQUI, core.PRIV_FILE))
                self._carrega_chave()
                messagebox.showinfo("Pronto",
                                    "Par criado.\n\nA chave PÚBLICA foi salva em %s e precisa ser "
                                    "embutida no plugin (Source/Common/License.cpp) para as novas "
                                    "licenças funcionarem." % core.PUB_FILE)

    # -------------------------------------------------------------- gerar
    def gerar(self):
        if self.priv is None:
            messagebox.showerror("Sem chave", "Carregue a chave privada primeiro.")
            return

        nome  = self.e_nome.get().strip()
        email = self.e_email.get().strip()
        mid   = self._valida_mid()
        nota  = self.e_nota.get().strip()

        if not nome:
            return self._erro("Preencha o nome do cliente.")
        if not email or "@" not in email:
            return self._erro("E-mail inválido.")
        if not mid:
            return self._erro("ID da máquina inválido — precisa de 12 dígitos hexadecimais "
                              "(o plugin mostra assim: TL-1147-89DF-849C).")

        dias = None
        if self.temp.get():
            try:
                dias = int(self.e_dias.get())
                if dias < 1:
                    raise ValueError
            except ValueError:
                return self._erro("Número de dias inválido.")

        import secrets
        from datetime import timedelta, timezone
        agora = datetime.now(timezone.utc)
        payload = {
            "v": 1, "prod": "transcriber-live",
            "nome": nome, "email": email, "mid": mid,
            "serial": secrets.token_hex(4).upper(),
            "iat": agora.strftime("%Y-%m-%d"),
        }
        if dias:
            payload["exp"] = (agora + timedelta(days=dias)).strftime("%Y-%m-%d")
        if nota:
            payload["nota"] = nota

        lic = core.assinar(payload, self.priv)

        self.txt.delete("1.0", "end")
        self.txt.insert("1.0", lic)
        self.ultima = (payload, lic)

        # salva .txt e registra no historico
        os.makedirs(PASTA_LICENCAS, exist_ok=True)
        destino = os.path.join(PASTA_LICENCAS, "licenca-%s-%s.txt" % (core.slug(nome), payload["serial"]))
        with open(destino, "w") as fh:
            fh.write(lic + "\n")
        self._registra(payload, destino)

        validade = "perpétua" if not dias else "%d dias (até %s)" % (dias, payload["exp"])
        self._ok("Licença gerada e salva em licencas/%s\nserial %s  ·  %s  ·  copie o texto e mande ao cliente."
                 % (os.path.basename(destino), payload["serial"], validade))
        self.copiar(silencioso=True)

    def _registra(self, payload, arquivo):
        novo = not os.path.exists(CSV_HISTORICO)
        with open(CSV_HISTORICO, "a", newline="") as fh:
            w = csv.writer(fh)
            if novo:
                w.writerow(["emitida_em", "nome", "email", "maquina", "serial",
                            "validade", "nota", "arquivo"])
            w.writerow([datetime.now().strftime("%Y-%m-%d %H:%M"), payload["nome"],
                        payload["email"], payload["mid"], payload["serial"],
                        payload.get("exp", "perpétua"), payload.get("nota", ""),
                        os.path.basename(arquivo)])
        self._carrega_historico()

    def copiar(self, silencioso=False):
        txt = self.txt.get("1.0", "end").strip()
        if not txt:
            return
        self.clipboard_clear()
        self.clipboard_append(txt)
        if not silencioso:
            self._ok("Licença copiada para a área de transferência.")

    def salvar(self):
        txt = self.txt.get("1.0", "end").strip()
        if not txt:
            return
        p = filedialog.asksaveasfilename(defaultextension=".txt", initialdir=PASTA_LICENCAS,
                                         initialfile="licenca.txt")
        if p:
            with open(p, "w") as fh:
                fh.write(txt + "\n")
            self._ok("Salva em %s" % p)

    def limpar(self):
        for e in (self.e_nome, self.e_email, self.e_mid, self.e_nota):
            e.delete(0, "end")
        self.txt.delete("1.0", "end")
        self.lbl_mid.configure(text="")
        self.lbl_status.configure(text="")
        self.temp.set(False)                      # volta ao padrão: perpétua
        self._toggle_dias()
        self.e_nome.focus_set()

    def _erro(self, msg):
        self.lbl_status.configure(text=msg, foreground=ALERTA)

    def _ok(self, msg):
        self.lbl_status.configure(text=msg, foreground=VERDE)

    # -------------------------------------------------------------- aba emitidas
    def _monta_historico(self):
        f = self.aba_hist

        topo = ttk.Frame(f, style="Card.TFrame")
        topo.pack(fill="x", padx=16, pady=(14, 4))
        ttk.Label(topo, text="Licenças emitidas", style="Head.TLabel").pack(side="left")
        self.lbl_hist = ttk.Label(topo, text="", style="Dim.TLabel")
        self.lbl_hist.pack(side="right")

        tabela = ttk.Frame(f, style="Card.TFrame")
        tabela.pack(fill="both", expand=True, padx=16, pady=(0, 8))

        cols = ("emitida_em", "nome", "email", "maquina", "serial", "validade", "nota")
        self.tree = ttk.Treeview(tabela, columns=cols, show="headings", height=15)
        larguras = (132, 128, 155, 138, 74, 86, 90)
        for c, w in zip(cols, larguras):
            self.tree.heading(c, text=c.replace("_", " "))
            self.tree.column(c, width=w, anchor="w", stretch=(c == "nota"))
        hbar = ttk.Scrollbar(tabela, orient="horizontal", command=self.tree.xview)
        self.tree.configure(xscrollcommand=hbar.set)
        self.tree.pack(fill="both", expand=True)
        hbar.pack(fill="x")

        barra = ttk.Frame(f, style="Card.TFrame")
        barra.pack(fill="x", padx=16, pady=(0, 12))
        ttk.Button(barra, text="Atualizar", command=self._carrega_historico).pack(side="left")
        ttk.Button(barra, text="Abrir CSV",
                   command=lambda: abrir_no_finder(CSV_HISTORICO)).pack(side="left", padx=8)
        ttk.Button(barra, text="Reabrir licença selecionada",
                   command=self._reabrir).pack(side="left")
        ttk.Button(barra, text="Backup da chave privada...",
                   command=self._backup_chave).pack(side="left", padx=8)
        self._carrega_historico()

    def _carrega_historico(self):
        if not hasattr(self, "tree"):
            return
        self.tree.delete(*self.tree.get_children())
        n = 0
        if os.path.exists(CSV_HISTORICO):
            with open(CSV_HISTORICO, newline="") as fh:
                for linha in list(csv.DictReader(fh))[::-1]:
                    self.tree.insert("", "end", values=[linha.get(c, "") for c in
                                                        ("emitida_em", "nome", "email", "maquina",
                                                         "serial", "validade", "nota")],
                                     tags=(linha.get("arquivo", ""),))
                    n += 1
        self.lbl_hist.configure(text="%d licença(s) emitida(s)" % n)

    def _backup_chave(self):
        """Copia a chave privada para onde o usuario escolher (pendrive, nuvem...)."""
        origem = self.priv_path.get()
        if not os.path.exists(origem):
            messagebox.showerror("Backup", "Não encontrei a chave privada para copiar.")
            return
        destino = filedialog.asksaveasfilename(
            title="Salvar backup da chave privada",
            initialfile="transcriberlive-private-BACKUP-%s.key" % datetime.now().strftime("%Y-%m-%d"),
            filetypes=[("Chave", "*.key")])
        if not destino:
            return
        try:
            with open(origem, "rb") as a, open(destino, "wb") as b:
                b.write(a.read())
            os.chmod(destino, 0o600)
            messagebox.showinfo("Backup",
                                "Backup salvo.\n\nGuarde em lugar seguro e privado: com esse arquivo "
                                "qualquer pessoa consegue emitir licenças do Transcriber Live.")
        except Exception as exc:
            messagebox.showerror("Backup", "Não deu para copiar: %s" % exc)

    def _reabrir(self):
        sel = self.tree.selection()
        if not sel:
            return
        arquivo = (self.tree.item(sel[0], "tags") or [""])[0]
        caminho = os.path.join(PASTA_LICENCAS, arquivo)
        if arquivo and os.path.exists(caminho):
            with open(caminho) as fh:
                self.conf_txt.delete("1.0", "end")
                self.conf_txt.insert("1.0", fh.read())
            self.verificar()
        else:
            messagebox.showinfo("Arquivo", "Não encontrei o .txt dessa licença em licencas/.")

    # -------------------------------------------------------------- aba verificar
    def _monta_verificar(self):
        f = self.aba_conf
        ttk.Label(f, text="Cole uma licença para conferir", style="Head.TLabel").pack(
            anchor="w", padx=16, pady=(16, 6))
        self.conf_txt = tk.Text(f, height=12, bg=WIDGET, fg=TEXT, insertbackground=TEXT,
                                relief="flat", font=("Menlo", 10), wrap="none",
                                highlightthickness=1, highlightbackground=OUTLINE)
        self.conf_txt.pack(fill="both", expand=True, padx=16)

        barra = ttk.Frame(f, style="Card.TFrame")
        barra.pack(fill="x", padx=16, pady=10)
        ttk.Button(barra, text="Verificar", command=self.verificar).pack(side="left")
        ttk.Button(barra, text="Colar", command=self._colar_conf).pack(side="left", padx=8)
        ttk.Button(barra, text="Abrir arquivo...", command=self._abrir_conf).pack(side="left")

        self.conf_res = ttk.Label(f, text="", style="Dim.TLabel", justify="left",
                                  wraplength=700)
        self.conf_res.pack(anchor="w", padx=16, pady=(0, 14))

    def _colar_conf(self):
        try:
            self.conf_txt.delete("1.0", "end")
            self.conf_txt.insert("1.0", self.clipboard_get())
        except tk.TclError:
            pass

    def _abrir_conf(self):
        p = filedialog.askopenfilename(initialdir=PASTA_LICENCAS,
                                       filetypes=[("Licença", "*.txt"), ("Todos", "*.*")])
        if p:
            with open(p) as fh:
                self.conf_txt.delete("1.0", "end")
                self.conf_txt.insert("1.0", fh.read())
            self.verificar()

    def verificar(self):
        if self.priv is None:
            self.conf_res.configure(text="Carregue a chave privada primeiro.", foreground=ALERTA)
            return
        pub = {"e": self.priv["e"], "n": self.priv["n"]}
        payload, erro = core.conferir(self.conf_txt.get("1.0", "end"), pub)
        if erro:
            self.conf_res.configure(text="INVÁLIDA — %s" % erro, foreground=ERRO)
            return
        linhas = ["VÁLIDA (assinatura confere)"]
        for k, rot in (("nome", "titular"), ("email", "e-mail"), ("mid", "máquina"),
                       ("serial", "serial"), ("iat", "emitida"), ("exp", "expira"),
                       ("nota", "nota")):
            if payload.get(k):
                linhas.append("%-8s %s" % (rot, payload[k]))
        if "exp" not in payload:
            linhas.append("%-8s %s" % ("validade", "perpétua"))
        self.conf_res.configure(text="\n".join(linhas), foreground=VERDE)


if __name__ == "__main__":
    App().mainloop()
