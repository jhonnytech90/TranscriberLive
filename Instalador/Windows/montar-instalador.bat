@echo off
REM ===========================================================================
REM  Transcriber Live - monta o instalador .exe do Windows
REM
REM  Duplo clique aqui. Ele procura os binarios, chama o Inno Setup e deixa o
REM  setup pronto em  saida\TranscriberLive-0.4-Windows-Setup.exe
REM
REM  Para apontar outra pasta:  montar-instalador.bat C:\caminho\dos\binarios
REM ===========================================================================
setlocal enabledelayedexpansion
cd /d "%~dp0"

echo.
echo ================================================================
echo  Transcriber Live - montador do instalador (Windows)
echo ================================================================
echo.

REM ---- 1. acha o Inno Setup -------------------------------------------------
set "ISCC="
for %%P in (
  "%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe"
  "%ProgramFiles%\Inno Setup 6\ISCC.exe"
  "%LocalAppData%\Programs\Inno Setup 6\ISCC.exe"
) do if exist %%P set "ISCC=%%~P"

if not defined ISCC (
  echo [X] Inno Setup 6 nao encontrado.
  echo.
  echo     Baixe em https://jrsoftware.org/isdl.php  ^(gratuito^)
  echo     ou instale pelo terminal:   winget install JRSoftware.InnoSetup
  echo.
  pause
  exit /b 1
)
echo  Inno Setup: "%ISCC%"

REM ---- 2. acha os binarios --------------------------------------------------
set "BIN=%~1"
if not defined BIN (
  for %%D in ("binarios" "..\..\build" "%USERPROFILE%\Downloads\TranscriberLive-Windows") do (
    if not defined ACHOU if exist "%%~D" (
      dir /s /b /ad "%%~D\*.vst3" >nul 2>&1 && ( set "BIN=%%~fD" & set "ACHOU=1" )
    )
  )
)

if not defined BIN (
  echo [X] Nao achei os binarios.
  echo.
  echo     Baixe o zip TranscriberLive-Windows do GitHub Actions e descompacte
  echo     numa pasta chamada "binarios" aqui do lado, ou arraste a pasta em
  echo     cima deste .bat.
  echo.
  pause
  exit /b 1
)
echo  Binarios  : "%BIN%"
echo.

REM ---- 3. compila -----------------------------------------------------------
if not exist "saida" mkdir "saida"
"%ISCC%" "/DBIN=%BIN%" "TranscriberLive.iss"
if errorlevel 1 (
  echo.
  echo [X] O Inno Setup falhou. A mensagem acima diz o motivo
  echo     ^(quase sempre e um bundle .vst3 faltando na pasta de binarios^).
  echo.
  pause
  exit /b 1
)

echo.
echo ================================================================
echo  Pronto:
dir /b "saida\*.exe"
echo ================================================================
echo.
echo  O setup NAO esta assinado: o Windows vai mostrar
echo  "O Windows protegeu o seu PC" na primeira execucao. Para tirar isso
echo  e preciso um certificado de assinatura de codigo ^(EV^).
echo.
start "" "saida"
pause
