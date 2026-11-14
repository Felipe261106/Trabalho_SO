@echo off
setlocal
cd /d "%~dp0"

echo ==========================================
echo       DESARME A BOMBA - INICIANDO
echo ==========================================

where gcc >nul 2>nul
if errorlevel 1 (
    echo ERRO: GCC nao encontrado.
    echo Instale o MinGW-w64 e adicione a pasta bin ao PATH.
    echo Depois feche e abra novamente o terminal.
    pause
    exit /b 1
)

echo.
echo [1/2] Compilando servidor...
gcc servidor.c -o servidor.exe -lws2_32
if errorlevel 1 (
    echo ERRO ao compilar servidor.c.
    pause
    exit /b 1
)

echo.
echo [2/2] Compilando cliente...
gcc cliente.c -o cliente.exe -lws2_32
if errorlevel 1 (
    echo ERRO ao compilar cliente.c.
    pause
    exit /b 1
)

echo.
echo Abrindo servidor e os dois jogadores...
start "Servidor - Desarme a Bomba" cmd /k "cd /d ""%~dp0"" && servidor.exe"
timeout /t 2 /nobreak >nul
start "Jogador 1 - Analista" cmd /k "cd /d ""%~dp0"" && cliente.exe"
timeout /t 1 /nobreak >nul
start "Jogador 2 - Decifrador" cmd /k "cd /d ""%~dp0"" && cliente.exe"

echo.
echo Foram abertas tres janelas.
echo Digite um nome em cada janela de jogador.
pause
