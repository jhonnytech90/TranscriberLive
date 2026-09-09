; ============================================================================
;  Transcriber Live — instalador do Windows (Inno Setup 6.3 ou mais novo)
; ============================================================================
;
;  Um unico .exe que:
;    - instala os plugins VST3 (Receiver e Display) em Common Files\VST3
;    - instala o aplicativo Display em Arquivos de Programas\Transcriber Live
;    - baixa os modelos de voz do Hugging Face, verificando o SHA-256 de cada um
;    - cria atalhos, desinstalador e a pasta de dados com permissao de escrita
;
;  Compilar:
;      iscc TranscriberLive.iss
;      iscc /DBIN=..\binarios TranscriberLive.iss     (outra pasta de binarios)
;
;  A pasta de binarios precisa conter, em algum destes formatos:
;      Transcriber Live Receiver.vst3\      (PASTA — VST3 no Windows e um bundle)
;      Transcriber Live Display.vst3\
;      Transcriber Live Display.exe
;  Serve tanto a pasta build/ do CMake quanto o zip do GitHub Actions descompactado.
;
; ============================================================================

#define AppName        "Transcriber Live"
#define AppVersion     "0.4"
#define AppPublisher   "Jhonatan Miikael"
#define AppSupport     "jhonyfascpinda@gmail.com"
#define AppId          "{{B7E4A21C-9F3D-4A76-8C51-3D2E7A64F1B9}"

#ifndef BIN
  #define BIN "binarios"
#endif

; ---- onde estao os bundles --------------------------------------------------
#if DirExists(BIN + "\VST3\Transcriber Live Receiver.vst3")
  #define RECEIVER_DIR BIN + "\VST3\Transcriber Live Receiver.vst3"
#elif DirExists(BIN + "\TranscriberLive_artefacts\Release\VST3\Transcriber Live Receiver.vst3")
  #define RECEIVER_DIR BIN + "\TranscriberLive_artefacts\Release\VST3\Transcriber Live Receiver.vst3"
#elif DirExists(BIN + "\Transcriber Live Receiver.vst3")
  #define RECEIVER_DIR BIN + "\Transcriber Live Receiver.vst3"
#else
  #error Nao achei "Transcriber Live Receiver.vst3". Use /DBIN=caminho-da-pasta
#endif

#if DirExists(BIN + "\VST3\Transcriber Live Display.vst3")
  #define DISPLAY_DIR BIN + "\VST3\Transcriber Live Display.vst3"
#elif DirExists(BIN + "\TranscriberLiveDisplay_artefacts\Release\VST3\Transcriber Live Display.vst3")
  #define DISPLAY_DIR BIN + "\TranscriberLiveDisplay_artefacts\Release\VST3\Transcriber Live Display.vst3"
#elif DirExists(BIN + "\Transcriber Live Display.vst3")
  #define DISPLAY_DIR BIN + "\Transcriber Live Display.vst3"
#else
  #error Nao achei "Transcriber Live Display.vst3"
#endif

#if FileExists(BIN + "\Transcriber Live Display.exe")
  #define DISPLAY_APP BIN + "\Transcriber Live Display.exe"
#elif FileExists(BIN + "\TranscriberLiveDisplayApp_artefacts\Release\Transcriber Live Display.exe")
  #define DISPLAY_APP BIN + "\TranscriberLiveDisplayApp_artefacts\Release\Transcriber Live Display.exe"
#elif FileExists(BIN + "\Release\Transcriber Live Display.exe")
  #define DISPLAY_APP BIN + "\Release\Transcriber Live Display.exe"
#else
  #error Nao achei "Transcriber Live Display.exe"
#endif

[Setup]
AppId={#AppId}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
AppSupportURL=mailto:{#AppSupport}
VersionInfoVersion=0.4.0.0
VersionInfoCompany={#AppPublisher}
VersionInfoDescription={#AppName} - instalador

DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
UninstallDisplayName={#AppName} {#AppVersion}
UninstallDisplayIcon={app}\Transcriber Live Display.exe

; VST3 vai para Arquivos de Programas: exige administrador
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0

OutputDir=saida
OutputBaseFilename=TranscriberLive-{#AppVersion}-Windows-Setup
Compression=lzma2/max
SolidCompression=yes

WizardStyle=modern
WizardImageFile=wizard-164x314.bmp,wizard-192x386.bmp,wizard-328x628.bmp,wizard-384x772.bmp
WizardImageStretch=no
WizardSmallImageFile=wizard-small-55.bmp,wizard-small-110.bmp,wizard-small-138.bmp
SetupIconFile=..\..\Resources\brand\ico\TranscriberLive.ico
InfoBeforeFile=..\Introducao.txt

[Languages]
Name: "brazilianportuguese"; MessagesFile: "compiler:Languages\BrazilianPortuguese.isl"

[Tasks]
Name: "desktopicon"; Description: "Criar atalho do Display na area de trabalho"; \
    GroupDescription: "Atalhos:"

[Files]
; ---- plugins VST3 (bundles: pastas inteiras) --------------------------------
Source: "{#RECEIVER_DIR}\*"; DestDir: "{commoncf64}\VST3\Transcriber Live Receiver.vst3"; \
    Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#DISPLAY_DIR}\*";  DestDir: "{commoncf64}\VST3\Transcriber Live Display.vst3"; \
    Flags: ignoreversion recursesubdirs createallsubdirs

; ---- aplicativo Display -----------------------------------------------------
Source: "{#DISPLAY_APP}"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\Introducao.txt"; DestDir: "{app}"; DestName: "Leia-me.txt"; Flags: ignoreversion isreadme

; ---- modelos ----------------------------------------------------------------
; Chegam em {tmp} pela pagina de download (secao [Code]).
;   external                 -> nao estao dentro do .exe
;   skipifsourcedoesntexist  -> silencioso se o usuario nao marcou aquele modelo
;   onlyifdoesntexist        -> nunca sobrescreve um modelo que o usuario ja tinha
;   uninsneveruninstall      -> desinstalar nao joga fora GB de download
Source: "{tmp}\ggml-silero-v5.1.2.bin";  DestDir: "{commonappdata}\TranscriberLive\models"; \
    Flags: external skipifsourcedoesntexist onlyifdoesntexist uninsneveruninstall
Source: "{tmp}\ggml-tiny.bin";           DestDir: "{commonappdata}\TranscriberLive\models"; \
    Flags: external skipifsourcedoesntexist onlyifdoesntexist uninsneveruninstall
Source: "{tmp}\ggml-base.bin";           DestDir: "{commonappdata}\TranscriberLive\models"; \
    Flags: external skipifsourcedoesntexist onlyifdoesntexist uninsneveruninstall
Source: "{tmp}\ggml-small.bin";          DestDir: "{commonappdata}\TranscriberLive\models"; \
    Flags: external skipifsourcedoesntexist onlyifdoesntexist uninsneveruninstall
Source: "{tmp}\ggml-medium.bin";         DestDir: "{commonappdata}\TranscriberLive\models"; \
    Flags: external skipifsourcedoesntexist onlyifdoesntexist uninsneveruninstall
Source: "{tmp}\ggml-large-v3-turbo.bin"; DestDir: "{commonappdata}\TranscriberLive\models"; \
    Flags: external skipifsourcedoesntexist onlyifdoesntexist uninsneveruninstall

[Dirs]
; os plugins gravam a licenca aqui e leem os modelos; todo usuario precisa escrever
Name: "{commonappdata}\TranscriberLive";        Permissions: users-modify
Name: "{commonappdata}\TranscriberLive\models"; Permissions: users-modify

[Icons]
Name: "{group}\Transcriber Live Display";       Filename: "{app}\Transcriber Live Display.exe"
Name: "{group}\Pasta dos modelos de voz";       Filename: "{commonappdata}\TranscriberLive\models"
Name: "{group}\Leia-me";                        Filename: "{app}\Leia-me.txt"
Name: "{autodesktop}\Transcriber Live Display"; Filename: "{app}\Transcriber Live Display.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\Transcriber Live Display.exe"; Description: "Abrir o Display agora"; \
    Flags: nowait postinstall skipifsilent

[UninstallDelete]
Type: filesandordirs; Name: "{commoncf64}\VST3\Transcriber Live Receiver.vst3"
Type: filesandordirs; Name: "{commoncf64}\VST3\Transcriber Live Display.vst3"

; ============================================================================
[Code]

const
  UltimoModelo = 5;   // indices 0..5 ; 0 = VAD (obrigatorio)

var
  PaginaModelos:  TInputOptionWizardPage;
  PaginaDownload: TDownloadWizardPage;

  MdArquivo: array[0..UltimoModelo] of String;
  MdUrl:     array[0..UltimoModelo] of String;
  MdSha:     array[0..UltimoModelo] of String;
  MdRotulo:  array[0..UltimoModelo] of String;

procedure DefineModelos;
var
  Base, Vad: String;
begin
  Base := 'https://huggingface.co/ggerganov/whisper.cpp/resolve/main/';
  Vad  := 'https://huggingface.co/ggml-org/whisper-vad/resolve/main/';

  MdArquivo[0] := 'ggml-silero-v5.1.2.bin';
  MdUrl[0]     := Vad + 'ggml-silero-v5.1.2.bin';
  MdSha[0]     := '29940d98d42b91fbd05ce489f3ecf7c72f0a42f027e4875919a28fb4c04ea2cf';
  MdRotulo[0]  := 'Detector de voz Silero - 0,9 MB (obrigatorio)';

  MdArquivo[1] := 'ggml-tiny.bin';
  MdUrl[1]     := Base + 'ggml-tiny.bin';
  MdSha[1]     := 'be07e048e1e599ad46341c8d2a135645097a538221678b7acdd1b1919c6e1b21';
  MdRotulo[1]  := 'tiny - 74 MB - o mais leve, erra mais';

  MdArquivo[2] := 'ggml-base.bin';
  MdUrl[2]     := Base + 'ggml-base.bin';
  MdSha[2]     := '60ed5bc3dd14eea856493d334349b405782ddcaf0028d4b5df4088345fba2efe';
  MdRotulo[2]  := 'base - 141 MB - bom para PC modesto';

  MdArquivo[3] := 'ggml-small.bin';
  MdUrl[3]     := Base + 'ggml-small.bin';
  MdSha[3]     := '1be3a9b2063867b937e64e2ec7483364a79917e157fa98c5d94b5c1fffea987b';
  MdRotulo[3]  := 'small - 465 MB - RECOMENDADO para show';

  MdArquivo[4] := 'ggml-medium.bin';
  MdUrl[4]     := Base + 'ggml-medium.bin';
  MdSha[4]     := '6c14d5adee5f86394037b4e4e8b59f1673b6cee10e3cf0b11bbdbee79c156208';
  MdRotulo[4]  := 'medium - 1,4 GB - mais preciso, exige CPU boa';

  MdArquivo[5] := 'ggml-large-v3-turbo.bin';
  MdUrl[5]     := Base + 'ggml-large-v3-turbo.bin';
  MdSha[5]     := '1fc70f774d38eb169993ac391eea357ef47c88757ef72ee5943879b7e8e2bc69';
  MdRotulo[5]  := 'large-v3-turbo - 1,5 GB - o melhor, so em maquina forte';
end;

function JaInstalado(const Arquivo: String): Boolean;
begin
  Result := FileExists(ExpandConstant('{commonappdata}\TranscriberLive\models\') + Arquivo);
end;

function OnDownloadProgress(const Url, FileName: String; const Progress, ProgressMax: Int64): Boolean;
begin
  if ProgressMax > 0 then
    PaginaDownload.SetText('Baixando ' + FileName,
      IntToStr(Progress div 1048576) + ' de ' + IntToStr(ProgressMax div 1048576) + ' MB')
  else
    PaginaDownload.SetText('Baixando ' + FileName, '');
  Result := True;
end;

procedure InitializeWizard;
var
  i: Integer;
begin
  DefineModelos;

  PaginaModelos := CreateInputOptionPage(wpSelectTasks,
    'Modelos de reconhecimento de voz',
    'Escolha o que sera baixado agora.',
    'O Transcriber Live nao manda audio para a internet: os modelos rodam dentro do seu' + #13#10 +
    'computador. O download acontece uma vez, aqui na instalacao, e cada arquivo tem o' + #13#10 +
    'SHA-256 conferido no fim.' + #13#10#13#10 +
    'Modelo maior acerta mais e pesa mais na CPU. Se estiver em duvida, deixe o small.',
    False, False);

  for i := 0 to UltimoModelo do
    if JaInstalado(MdArquivo[i]) then
      PaginaModelos.Add(MdRotulo[i] + '   [ja esta instalado]')
    else
      PaginaModelos.Add(MdRotulo[i]);

  for i := 0 to UltimoModelo do
    PaginaModelos.Values[i] := False;

  // VAD obrigatorio (desmarcavel so se ja estiver no disco); small como padrao
  if not JaInstalado(MdArquivo[0]) then
  begin
    PaginaModelos.Values[0] := True;
    PaginaModelos.CheckListBox.ItemEnabled[0] := False;   // nao deixa desmarcar
  end;
  if not JaInstalado(MdArquivo[3]) then
    PaginaModelos.Values[3] := True;

  PaginaDownload := CreateDownloadPage(
    'Baixando os modelos de voz',
    'Pode levar alguns minutos, dependendo da sua internet.',
    @OnDownloadProgress);
end;

function NextButtonClick(CurPageID: Integer): Boolean;
var
  i, comModelo, resposta: Integer;
begin
  Result := True;

  if CurPageID = PaginaModelos.ID then
  begin
    comModelo := 0;
    for i := 1 to UltimoModelo do
      if PaginaModelos.Values[i] or JaInstalado(MdArquivo[i]) then
        comModelo := comModelo + 1;

    if comModelo = 0 then
      if MsgBox('Nenhum modelo de transcricao selecionado.' + #13#10#13#10 +
                'Sem modelo o Receiver nao transcreve - o audio passa pelo canal, mas nao' + #13#10 +
                'sai texto. Voce pode colocar um ggml-*.bin na pasta de modelos depois.' + #13#10#13#10 +
                'Continuar assim?', mbConfirmation, MB_YESNO) = IDNO then
        Result := False;
    Exit;
  end;

  if CurPageID = wpReady then
  begin
    PaginaDownload.Clear;

    for i := 0 to UltimoModelo do
      if PaginaModelos.Values[i] and not JaInstalado(MdArquivo[i]) then
        PaginaDownload.Add(MdUrl[i], MdArquivo[i], MdSha[i]);

    PaginaDownload.Show;
    try
      try
        PaginaDownload.Download;
      except
        if PaginaDownload.AbortedByUser then
          Log('Download cancelado pelo usuario.')
        else
          resposta := SuppressibleMsgBox(
            'Nao consegui baixar os modelos:' + #13#10#13#10 + GetExceptionMessage + #13#10#13#10 +
            'A instalacao continua sem eles. Depois voce pode baixar em' + #13#10 +
            'huggingface.co/ggerganov/whisper.cpp e colocar os ggml-*.bin em' + #13#10 +
            ExpandConstant('{commonappdata}\TranscriberLive\models'),
            mbCriticalError, MB_OK, IDOK);
      end;
      Result := True;   // download nunca aborta a instalacao dos plugins
    finally
      PaginaDownload.Hide;
    end;
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  pasta: String;
begin
  if CurUninstallStep = usPostUninstall then
  begin
    pasta := ExpandConstant('{commonappdata}\TranscriberLive\models');
    if DirExists(pasta) then
      if MsgBox('Remover tambem os modelos de voz baixados?' + #13#10#13#10 + pasta + #13#10#13#10 +
                'Se pretende reinstalar, responda Nao para nao baixar tudo de novo.',
                mbConfirmation, MB_YESNO) = IDYES then
        DelTree(pasta, True, True, True);
    // a licenca fica no disco: e do usuario e presa a esta maquina
  end;
end;
