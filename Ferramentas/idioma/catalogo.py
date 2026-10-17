# -*- coding: utf-8 -*-
"""Catálogo de tradução da interface do plugin: português -> inglês.

A chave é o próprio texto em português, exatamente como está no código.
Se o texto mudar lá, mude aqui também — senão ele volta a aparecer em
português dentro da interface em inglês, calado, sem erro de compilação.

Depois de editar:  python3 Ferramentas/idioma/gerar.py
"""

EN = {
# ---------------------------------------------------------------- Receiver
"FALA": "SPEECH",
"Nome": "Name",
"Import.": "Priority",
"Modo": "Mode",
"Sensib. VAD": "VAD sens.",
"Fim de frase": "End of phrase",
"ex.: Cantor, Baixo...": "e.g. Singer, Bass...",
"Normal": "Normal",
"Importante": "Important",
"Urgente": "Urgent",
"Cor": "Colour",
"Licença": "License",
"Parciais": "Partials",
"Canal": "Channel",
"Mostra um QR Code para abrir o Display no celular ou tablet":
  "Shows a QR code to open the Display on a phone or tablet",
"Abre a pasta de logs e copia o diagnostico para colar no suporte":
  "Opens the log folder and copies the diagnostics to paste to support",
"Idioma da interface — Interface language":
  "Interface language — Idioma da interface",
"Automático (sistema)": "Automatic (system)",
"Falha na inicialização: ": "Startup failed: ",
"Sem licença — o áudio passa intacto, mas nada é transcrito.":
  "No license — the audio passes through untouched, but nothing is transcribed.",
"transcrevendo...": "transcribing...",
"Automático: ": "Automatic: ",
" — esta máquina não acompanhava o tempo real.":
  " — this machine wasn't keeping up with real time.",
"aliviei o processamento": "eased off the processing",
"liguei o modo econômico": "turned on economy mode",

# --- botão Log
"O log está desligado (existe um arquivo \"sem-log\" na pasta de dados, ou a pasta não tem permissão de escrita).":
  "Logging is off (there is a \"sem-log\" file in the data folder, or the folder isn't writable).",
"Diagnóstico copiado — é só colar no WhatsApp ou no e-mail do suporte.\n\nA pasta com os arquivos completos foi aberta:\n":
  "Diagnostics copied — just paste it into WhatsApp or an email to support.\n\nThe folder with the full files has been opened:\n",

# --- painel do QR
"Copiar endereço": "Copy address",
"Celulares podem escrever": "Phones can write",
"Sem precisar do QR (vale digitar o endereço)": "No QR needed (typing the address works)",
"Trocar código": "New code",
"Outra rede (%1 de %2)": "Other network (%1 of %2)",
"Esta máquina tem mais de uma rede. Se o celular não abrir a página, troque aqui até achar a rede em que ele está.":
  "This machine is on more than one network. If the phone doesn't open the page, switch here until you find the one it's on.",
"Para PC, que nao le QR Code. Com isto ligado, quem digitar o endereço no navegador também escreve — inclusive quem só descobriu o endereço na rede do local.":
  "For PCs, which can't read a QR code. With this on, anyone who types the address in a browser can write too — including anyone who merely found the address on the venue's network.",
"Desligado, ninguém escreve pelo celular, nem quem já leu o QR.":
  "Off, nobody writes from a phone, not even someone who already scanned the QR.",
"Sorteia um código novo: os aparelhos que já entraram param de escrever até lerem o QR de novo. Serve para cortar o acesso de alguém sem desligar a função para todos.":
  "Draws a new code: devices already in stop writing until they scan the QR again. Use it to cut one person's access without turning the feature off for everyone.",
"Sem rede": "No network",
"Esta máquina não tem endereço de rede local. Conecte no mesmo Wi-Fi do celular.":
  "This machine has no local network address. Connect it to the same Wi-Fi as the phone.",
"Endereço para o celular": "Address for the phone",
"Não consegui gerar o QR para este endereço. Digite no navegador.":
  "I couldn't generate the QR for this address. Type it in the browser.",
"Aponte a câmera do celular": "Point the phone's camera",
"Display em outra máquina: aqui o QR abre só para leitura. Para escrever, use o QR do Receiver daquela máquina.":
  "Display on another machine: this QR opens read-only. To write, use the QR from the Receiver on that machine.",
"Quem ler o QR pode escrever no telão. Quem só digitar o endereço vê a conversa, mas não escreve.":
  "Whoever scans the QR can write on the screen. Whoever only types the address sees the conversation but can't write.",
"Ainda não peguei o código de escrita do Display. Abra de novo em alguns segundos.":
  "I haven't picked up the Display's writing code yet. Open this again in a few seconds.",

# ---------------------------------------------------------------- Display
"Flash": "Flash",
"Limpar tudo": "Clear all",
"Rede...": "Network...",
"URGENTE": "URGENT",
"Nenhuma mensagem ainda. Os Receivers aparecem na barra lateral quando conectarem.":
  "No messages yet. Receivers show up in the sidebar when they connect.",
"Todos": "All",
" canais": " channels",
" - urgente": " - urgent",
" - importante": " - important",
" - offline": " - offline",
"PARTICIPANTES": "PARTICIPANTS",
"espelhando o Display principal desta maquina": "mirroring the main Display on this machine",
" em uso por outro programa - tentando...": " in use by another program - retrying...",
"Celular/tablet: ": "Phone/tablet: ",
" conectados)": " connected)",
"Porta UDP (Receivers):": "UDP port (Receivers):",
"Porta web (celular):": "Web port (phone):",
"Outro computador (IP):": "Other computer (IP):",
"Servidor web ligado": "Web server on",
"Aplicar": "Apply",
"Mostra aqui também os Receivers daquele computador. Serve para o técnico de P.A. e o de monitor conversarem entre dois PCs: cada um aponta para o outro. Vale tanto neste plugin dentro do host quanto no app Display. Se a outra máquina estiver em outra porta web, escreva ip:porta. Vazio = só esta máquina.":
  "Also shows the Receivers from that computer here. It's for the FOH and monitor engineers talking between two PCs: each points at the other. Works both in this plugin inside the host and in the Display app. If the other machine is on a different web port, write ip:port. Empty = this machine only.",
"conectado a ": "connected to ",
"sem resposta de ": "no answer from ",

# ---------------------------------------------------------------- Modelos
"Velocidade máxima": "Maximum speed",
"Erra muito em português. Só para teste.": "Gets a lot wrong in Portuguese. For testing only.",
"Mais rápido": "Faster",
"Para máquina fraca ou Mac Intel. Reconhece menos.": "For a weak machine or an Intel Mac. Recognises less.",
"Equilibrado": "Balanced",
"Bom reconhecimento com folga de CPU. Serve na maioria.": "Good recognition with CPU to spare. Fine for most.",
"Melhor transcrição": "Better transcription",
"Reconhece bem melhor. Exige máquina boa.": "Recognises a lot better. Needs a good machine.",
"Máxima precisão": "Maximum accuracy",
"O que mais acerta sem perder o tempo real. Apple Silicon ou PC forte.":
  "The one that gets most of it right without losing real time. Apple Silicon or a strong PC.",
"Máxima precisão (pesado)": "Maximum accuracy (heavy)",
"O teto do reconhecimento. Pode não acompanhar o show.":
  "The ceiling of recognition. May not keep up with the show.",
"Automático": "Automatic",
"Desempenho": "Performance",
"Precisão": "Accuracy",
"Começa no melhor que esta máquina aguenta e alivia sozinho se começar a atrasar. Na dúvida, deixe neste.":
  "Starts at the best this machine can handle and eases off on its own if it starts lagging. When in doubt, leave it here.",
"Responde o mais rápido possível. Erra mais palavra, mas não atrasa nem na máquina mais fraca.":
  "Answers as fast as possible. Gets more words wrong, but never lags, even on the weakest machine.",
"O melhor reconhecimento disponível. Em máquina fraca a legenda pode atrasar — e não vai se rebaixar sozinho.":
  "The best recognition available. On a weak machine the text may lag — and it won't step down on its own.",

# ---------------------------------------------------------------- Licença
"LICENÇA": "LICENSE",
"ID desta máquina": "This machine's ID",
"Cole aqui a chave que você recebeu": "Paste the key you received here",
"ID copiado — mande para o desenvolvedor junto com seu nome e e-mail.":
  "ID copied — send it to the developer along with your name and email.",
"Ativo. Licenciado para ": "Active. Licensed to ",
"  ·  emitida em ": "  ·  issued on ",
"perpétua": "perpetual",
"válida até ": "valid until ",
"Copiar": "Copy",
"Colar": "Paste",
"Ativar": "Activate",
"Remover licença": "Remove license",
"Fechar": "Close",
}

# Iguais nas duas linguas, mas no catálogo de propósito: assim o conferidor
# não acusa falta, e fica registrado que a decisão foi deixar igual.
EN.update({
"Gate": "Gate",
"serial ": "serial ",
"Idioma": "Language",
"ex.: 192.168.0.20 ou 192.168.0.20:47801": "e.g. 192.168.0.20 or 192.168.0.20:47801",
})
