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

# --- painel do QR
"Copiar endereço": "Copy address",
"Sem rede": "No network",
"Esta máquina não tem endereço de rede local. Conecte no mesmo Wi-Fi do celular.":
  "This machine has no local network address. Connect it to the same Wi-Fi as the phone.",
"Endereço para o celular": "Address for the phone",
"Não consegui gerar o QR para este endereço. Digite no navegador.":
  "I couldn't generate the QR for this address. Type it in the browser.",

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

# Painel do QR e escolha de rede (versão nova, sem trava de escrita)
EN.update({
"Rede desta máquina": "This machine's network",
"Rede local": "Local network",
"Rede virtual (VPN ou máquina virtual)": "Virtual network (VPN or virtual machine)",
"Rede do provedor (não e a rede da sala)": "Carrier network (not the room's network)",
"Endereço público (não e a rede da sala)": "Public address (not the room's network)",
"Computador: ": "Computer: ",
"O celular provavelmente nao alcanca esta. Prefira uma rede local.":
  "The phone probably can't reach this one. Prefer a local network.",
"Aponte a câmera do celular. Quem abrir a página, pela câmera ou digitando o endereço, já pode escrever.":
  "Point the phone's camera. Anyone who opens the page, by camera or by typing the address, can write.",
"Display em outra máquina: o QR abre a página de lá.":
  "Display on another machine: the QR opens that machine's page.",

# -- placa de rede (o usuario precisa saber qual IP e o cabo e qual e o Wi-Fi)
"Wi-Fi": "Wi-Fi",
"Cabeada": "Wired",
"Ponte": "Bridge",
"Virtual": "Virtual",
"Placa: ": "Adapter: ",

# -- envio de log para o suporte
"Enviar log para o suporte": "Send log to support",
"Isto é o que vai para o suporte:": "This is what goes to support:",
"Nome: ": "Name: ",
"E-mail: ": "E-mail: ",
"Máquina: ": "Machine: ",
"Versão: ": "Version: ",
"Programa: ": "Host: ",
"Arquivos: ": "Files: ",
"(sem licença instalada)": "(no licence installed)",
"Os logs guardam o que a ferramenta fez: horários, tempos de transcrição e erros. Não vai áudio nem o que foi falado.":
  "The logs record what the tool did: timestamps, transcription times and errors. No audio and nothing that was said.",
"O que aconteceu? (opcional)": "What happened? (optional)",
"Enviar": "Send",
"Abrir pasta": "Open folder",
"Cancelar": "Cancel",
"Ainda não há nada gravado para enviar.": "There is nothing recorded to send yet.",
"Log enviado. Obrigado — já estamos com ele aqui.": "Log sent. Thank you — we have it here.",
"Você ainda pode mandar os arquivos à mão: use Abrir pasta.":
  "You can still send the files by hand: use Open folder.",
"Não encontrei nenhum log para enviar.": "I found no log to send.",
"Não consegui falar com o site. Confira a internet desta máquina.":
  "I could not reach the site. Check this machine's internet connection.",
"O site recusou o envio.": "The site refused the upload.",

# -- aviso de versao nova na barra de status
"Versão ": "Version ",
" disponível.": " available.",
"Baixar": "Download",
"Ignorar": "Dismiss",

# -- barra de escrever do Display
"Monitor": "Monitor",
"Escreva aqui e tecle Enter": "Type here and press Enter",
"Não consegui enviar. Confira a rede.": "Could not send. Check the network.",

# -- mensagens de licenca (antes saiam com o acento quebrado: "licenÃ§a")
"Nenhuma licença informada.": "No license provided.",
"Formato inválido (o texto parece incompleto).": "Invalid format (the text looks incomplete).",
"Conteúdo ilegível — copie a licença inteira, incluindo todas as linhas.": "Unreadable content — copy the whole license, including every line.",
"Assinatura inválida — esta licença não foi emitida para o Transcriber Live.": "Invalid signature — this license was not issued for Transcriber Live.",
"Licença de outro produto.": "License for another product.",
"Conteúdo ilegível.": "Unreadable content.",
"Esta licença foi emitida para outro computador (": "This license was issued for another computer (",
"Licença expirada em ": "License expired on ",
"Nenhuma licença instalada nesta máquina.": "No license installed on this machine.",
"A pasta de dados está bloqueada para o seu usuário (": "The data folder is locked for your user (",
") — por isso a ativação não grava.": ") — so activation cannot save.",
"A licença é válida, mas não consegui gravar em ": "The license is valid, but I could not save it to ",
" — a pasta não tem permissão de escrita para o seu usuário.": " — the folder has no write permission for your user.",
"Mais opções: versão, licença e idioma": "More options: version, license and language",
"Versão instalada: ": "Installed version: ",
"Não consegui verificar atualizações": "Could not check for updates",
"Verificando atualizações...": "Checking for updates...",
"Última versão: ": "Latest version: ",
"Baixar a versão ": "Download version ",
"Você está na versão mais recente": "You are on the latest version",
"Verificar de novo": "Check again",
"Licença...": "License...",
"Idioma — Language": "Language — Idioma",
})
