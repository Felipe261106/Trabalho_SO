# Desarme a Bomba — mesma máquina (Windows)

Jogo cooperativo em C para dois jogadores no mesmo computador. O servidor e os dois clientes são executados em três terminais diferentes, usando sockets TCP locais (`127.0.0.1`).

## Arquivos
- `servidor.c`: inicia a partida, gera o código, distribui pistas, valida tentativas e determina o resultado.
- `cliente.c`: programa de cada jogador.
- `iniciar_jogo.bat`: abre o servidor e dois clientes em janelas separadas.
- `.vscode/tasks.json`: tarefas opcionais de compilação no VS Code.

## Como jogar
1. Compile os programas (instruções abaixo).
2. Dê dois cliques em `iniciar_jogo.bat`, ou siga o método manual.
3. Na janela do servidor, espere os clientes se conectarem.
4. Digite um nome na janela de cada jogador.
5. O primeiro cliente é o **Analista**, que recebe uma pista secreta.
6. O segundo cliente é o **Decifrador**, que recebe a mensagem repassada pelo Analista e tenta acertar o código.
7. O grupo tem até 8 tentativas e 180 segundos para descobrir o código de quatro dígitos. Assim que a partida começa, um cronômetro regressivo (03:00) aparece no terminal de cada jogador e continua atualizando enquanto o jogador digita.

## Requisitos
- Windows 10 ou 11.
- VS Code (opcional, mas recomendado).
- GCC/MinGW-w64 instalado e disponível no PATH.
- O código usa Winsock (`winsock2.h`), fornecido pelo Windows. O cronômetro usa `GetTickCount()`, compatível com GCC/MinGW.

## Compilar no VS Code
Abra a pasta do projeto no VS Code e abra o terminal com `Ctrl+J`. Execute:

```powershell
gcc servidor.c -o servidor.exe -lws2_32
gcc cliente.c -o cliente.exe -lws2_32
```

Se o comando `gcc` não for reconhecido, instale/configure o GCC MinGW-w64 e adicione a pasta `bin` ao PATH.

## Executar manualmente
Abra três terminais na pasta do projeto:

**Terminal 1 — servidor**
```powershell
.\servidor.exe
```

**Terminal 2 — jogador 1**
```powershell
.\cliente.exe
```

**Terminal 3 — jogador 2**
```powershell
.\cliente.exe
```

Os clientes conectam automaticamente a `127.0.0.1`, que representa o próprio computador. O servidor deve ser iniciado antes dos clientes.

## Como a comunicação funciona
Os dois clientes se conectam ao servidor usando sockets TCP pela interface local `127.0.0.1` e porta `54000`. O protocolo usa mensagens de texto com códigos, por exemplo:
- `NOME|...`: solicitação de nome.
- `ROLE|...`: informa o papel do jogador.
- `CLUEPRIVATE|...`: pista secreta do Analista.
- `ASK|MESSAGE|...`: pede ao Analista uma mensagem para o colega.
- `HINT|...`: pista encaminhada ao Decifrador.
- `ASK|GUESS|...`: pede uma tentativa de código.
- `RESULT|...`: informa o resultado da tentativa.
- `FIM|...`: informa o resultado final.

## Memória compartilhada e mutex
O servidor cria uma região de memória compartilhada do Windows com `CreateFileMappingA` e `MapViewOfFile`, usada para manter informações da partida. Um mutex do Windows (`CreateMutexA`, `WaitForSingleObject` e `ReleaseMutex`) protege o acesso às informações críticas.

**Observação importante para o trabalho:** a memória compartilhada é criada pelo servidor para armazenar o estado do jogo; os clientes trocam suas ações e resultados com o servidor por sockets. Se o professor exigir que os dois processos clientes também abram diretamente a mesma região de memória compartilhada, será necessário ampliar a implementação para que eles mapeiem explicitamente essa região.

## Sugestão para a apresentação
Mostre as três janelas: servidor, Analista e Decifrador. Grave a entrada dos nomes, a pista enviada pelo Analista, uma tentativa do Decifrador e o resultado final.

## Observação
O projeto foi preparado para Windows, mas precisa ser compilado e testado no computador usado na apresentação.
