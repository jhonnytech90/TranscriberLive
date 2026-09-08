#!/bin/bash
# Duplo clique aqui para abrir o Gerador de Licencas do Transcriber Live.
cd "$(dirname "$0")" || exit 1
if /usr/bin/python3 -c 'import tkinter' >/dev/null 2>&1; then
    exec /usr/bin/python3 licenca_gui.py
elif command -v python3 >/dev/null 2>&1 && python3 -c 'import tkinter' >/dev/null 2>&1; then
    exec python3 licenca_gui.py
else
    echo "Python 3 com tkinter nao encontrado."
    echo "No macOS instale com:  brew install python-tk"
    read -r -p "Enter para fechar..." _
fi
