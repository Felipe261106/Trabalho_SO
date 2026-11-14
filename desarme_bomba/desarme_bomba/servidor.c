#define _CRT_SECURE_NO_WARNINGS
#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#pragma comment(lib, "Ws2_32.lib")

#define PORTA 54000
#define CLIENTES 2
#define TAM 1024
#define MAX_TENTATIVAS 8
#define TEMPO_PARTIDA 180

typedef struct {
    char codigo[5];
    int tentativas;
    int max_tentativas;
    int estado; /* 0 = em andamento, 1 = desarmada, 2 = explodiu/tempo esgotado */
    int rodada;
    char ultima_pista[160];
} EstadoBomba;

typedef struct {
    SOCKET socket;
    char nome[64];
} Cliente;

static HANDLE hMap = NULL;
static HANDLE hMutex = NULL;
static EstadoBomba *bomba = NULL;
static Cliente jogadores[CLIENTES];

static int enviar_linha(SOCKET s, const char *texto) {
    char buffer[TAM];
    int n = snprintf(buffer, sizeof(buffer), "%s\n", texto);
    if (n <= 0 || n >= (int)sizeof(buffer)) return 0;
    int total = 0;
    while (total < n) {
        int enviados = send(s, buffer + total, n - total, 0);
        if (enviados == SOCKET_ERROR || enviados == 0) return 0;
        total += enviados;
    }
    return 1;
}

/* deadline=0 significa sem limite; caso contrário, a espera termina no prazo. */
static int receber_linha(SOCKET s, char *buffer, int tamanho, time_t deadline) {
    int i = 0;
    while (i < tamanho - 1) {
        if (deadline != 0) {
            time_t agora = time(NULL);
            int restante = (int)(deadline - agora);
            if (restante <= 0) return -1;
            fd_set conjunto;
            FD_ZERO(&conjunto);
            FD_SET(s, &conjunto);
            struct timeval tv;
            tv.tv_sec = restante;
            tv.tv_usec = 0;
            int pronto = select(0, &conjunto, NULL, NULL, &tv);
            if (pronto == 0) return -1;
            if (pronto == SOCKET_ERROR) return 0;
        }

        char c;
        int n = recv(s, &c, 1, 0);
        if (n <= 0) return 0;
        if (c == '\n') break;
        if (c != '\r') buffer[i++] = c;
    }
    buffer[i] = '\0';
    return 1;
}

static void enviar_todos(const char *texto) {
    for (int i = 0; i < CLIENTES; i++) {
        if (jogadores[i].socket != INVALID_SOCKET)
            enviar_linha(jogadores[i].socket, texto);
    }
}

static void travar(void) {
    WaitForSingleObject(hMutex, INFINITE);
}

static void destravar(void) {
    ReleaseMutex(hMutex);
}

static void criar_codigo(void) {
    for (int i = 0; i < 4; i++) bomba->codigo[i] = (char)('0' + rand() % 10);
    bomba->codigo[4] = '\0';
    bomba->tentativas = 0;
    bomba->max_tentativas = MAX_TENTATIVAS;
    bomba->estado = 0;
    bomba->rodada = 0;
    bomba->ultima_pista[0] = '\0';
}

static int codigo_valido(const char *s) {
    if (strlen(s) != 4) return 0;
    for (int i = 0; i < 4; i++)
        if (!isdigit((unsigned char)s[i])) return 0;
    return 1;
}

/* Pistas são calculadas pelo servidor e enviadas apenas ao Analista. */
static void gerar_pista(int rodada, char *pista, size_t tamanho) {
    int d[4], soma = 0, pares = 0, repetido = 0;
    for (int i = 0; i < 4; i++) {
        d[i] = bomba->codigo[i] - '0';
        soma += d[i];
        if (d[i] % 2 == 0) pares++;
        for (int j = 0; j < i; j++)
            if (d[i] == d[j]) repetido = 1;
    }

    switch (rodada % 8) {
        case 0:
            snprintf(pista, tamanho, "O primeiro digito e %s.", d[0] % 2 == 0 ? "PAR" : "IMPAR");
            break;
        case 1:
            snprintf(pista, tamanho, "A soma dos quatro digitos e %d.", soma);
            break;
        case 2:
            snprintf(pista, tamanho, "O ultimo digito e %d.", d[3]);
            break;
        case 3:
            snprintf(pista, tamanho, "O codigo possui %d digito(s) par(es).", pares);
            break;
        case 4:
            snprintf(pista, tamanho, "O primeiro digito e %s que o ultimo.",
                d[0] > d[3] ? "MAIOR" : (d[0] < d[3] ? "MENOR" : "IGUAL"));
            break;
        case 5:
            snprintf(pista, tamanho, "O segundo digito e %s.", d[1] % 2 == 0 ? "PAR" : "IMPAR");
            break;
        case 6:
            snprintf(pista, tamanho, "O codigo %s digitos repetidos.", repetido ? "POSSUI" : "NAO POSSUI");
            break;
        default:
            snprintf(pista, tamanho, "O terceiro digito e %d.", d[2]);
            break;
    }
}

static void finalizar(const char *mensagem, int mostrarCodigo) {
    char fim[TAM];
    travar();
    bomba->estado = (strstr(mensagem, "DESARMADA") != NULL) ? 1 : 2;
    if (mostrarCodigo)
        snprintf(fim, sizeof(fim), "FIM|%s Codigo correto: %s", mensagem, bomba->codigo);
    else
        snprintf(fim, sizeof(fim), "FIM|%s", mensagem);
    destravar();
    enviar_todos(fim);
}

int main(void) {
    WSADATA wsa;
    SOCKET servidor = INVALID_SOCKET;
    struct sockaddr_in endereco;
    time_t deadline;

    srand((unsigned int)time(NULL));
    for (int i = 0; i < CLIENTES; i++) jogadores[i].socket = INVALID_SOCKET;

    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        printf("Erro ao iniciar Winsock.\n");
        return 1;
    }

    hMap = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
                              0, sizeof(EstadoBomba), "Local\\DesarmeBombaEstado");
    if (hMap == NULL) {
        printf("Erro ao criar memoria compartilhada: %lu\n", GetLastError());
        WSACleanup(); return 1;
    }
    bomba = (EstadoBomba *)MapViewOfFile(hMap, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(EstadoBomba));
    hMutex = CreateMutexA(NULL, FALSE, "Local\\DesarmeBombaMutex");
    if (bomba == NULL || hMutex == NULL) {
        printf("Erro ao mapear memoria ou criar mutex.\n");
        if (bomba) UnmapViewOfFile(bomba);
        if (hMap) CloseHandle(hMap);
        WSACleanup(); return 1;
    }

    servidor = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (servidor == INVALID_SOCKET) {
        printf("Erro ao criar socket.\n");
        UnmapViewOfFile(bomba); CloseHandle(hMap); CloseHandle(hMutex); WSACleanup();
        return 1;
    }

    memset(&endereco, 0, sizeof(endereco));
    endereco.sin_family = AF_INET;
    endereco.sin_addr.s_addr = htonl(INADDR_ANY);
    endereco.sin_port = htons(PORTA);

    if (bind(servidor, (struct sockaddr *)&endereco, sizeof(endereco)) == SOCKET_ERROR) {
        printf("Erro no bind. Codigo Winsock: %d\n", WSAGetLastError());
        closesocket(servidor); UnmapViewOfFile(bomba); CloseHandle(hMap); CloseHandle(hMutex); WSACleanup();
        return 1;
    }
    if (listen(servidor, CLIENTES) == SOCKET_ERROR) {
        printf("Erro no listen.\n");
        closesocket(servidor); UnmapViewOfFile(bomba); CloseHandle(hMap); CloseHandle(hMutex); WSACleanup();
        return 1;
    }

    printf("=== SERVIDOR: DESARME A BOMBA ===\n");
    printf("Aguardando dois jogadores na porta TCP %d...\n", PORTA);

    for (int i = 0; i < CLIENTES; i++) {
        struct sockaddr_in addrCliente;
        int tamanhoAddr = sizeof(addrCliente);
        SOCKET s = accept(servidor, (struct sockaddr *)&addrCliente, &tamanhoAddr);
        if (s == INVALID_SOCKET) {
            printf("Erro ao aceitar cliente.\n");
            break;
        }
        jogadores[i].socket = s;
        enviar_linha(s, "NOME|Digite seu nome:");
        char linha[TAM];
        int recebido = receber_linha(s, linha, sizeof(linha), 0);
        if (recebido != 1) {
            printf("Cliente desconectou durante a entrada do nome.\n");
            closesocket(s); jogadores[i].socket = INVALID_SOCKET;
            break;
        }
        strncpy(jogadores[i].nome, linha, sizeof(jogadores[i].nome) - 1);
        jogadores[i].nome[sizeof(jogadores[i].nome) - 1] = '\0';
        if (jogadores[i].nome[0] == '\0')
            snprintf(jogadores[i].nome, sizeof(jogadores[i].nome), "Jogador%d", i + 1);
        printf("Jogador %d conectado: %s\n", i + 1, jogadores[i].nome);
    }

    if (jogadores[0].socket == INVALID_SOCKET || jogadores[1].socket == INVALID_SOCKET) {
        printf("A partida precisa de dois clientes. Encerrando.\n");
        goto limpeza;
    }

    travar();
    criar_codigo();
    destravar();

    enviar_linha(jogadores[0].socket, "ROLE|ANALISTA|Voce recebe pistas e deve repassa-las ao colega.");
    enviar_linha(jogadores[1].socket, "ROLE|DECIFRADOR|Voce recebe as pistas enviadas pelo colega e tenta o codigo.");
    enviar_todos("INFO|Modo cooperativo: o Analista recebe uma pista privada por rodada e a transmite ao Decifrador.");
    enviar_todos("INFO|O codigo tem 4 digitos. Existem 8 tentativas e limite total de 180 segundos.");

    deadline = time(NULL) + TEMPO_PARTIDA;
    for (int rodada = 0; rodada < MAX_TENTATIVAS; rodada++) {
        char pista[TAM], pergunta[TAM], linha[TAM], mensagem[TAM];
        int recebido;

        if (time(NULL) >= deadline) {
            finalizar("TEMPO ESGOTADO! A bomba explodiu.", 1);
            break;
        }

        travar();
        bomba->rodada = rodada + 1;
        gerar_pista(rodada, pista, sizeof(pista));
        destravar();

        snprintf(mensagem, sizeof(mensagem), "INFO|Rodada %d de %d.", rodada + 1, MAX_TENTATIVAS);
        enviar_todos(mensagem);
        snprintf(mensagem, sizeof(mensagem), "CLUEPRIVATE|Pista secreta: %s", pista);
        enviar_linha(jogadores[0].socket, mensagem);
        enviar_linha(jogadores[1].socket, "INFO|Aguarde a pista enviada pelo Analista.");

        enviar_linha(jogadores[0].socket, "ASK|MESSAGE|Digite a pista que deseja enviar ao colega (maximo 120 caracteres):");
        recebido = receber_linha(jogadores[0].socket, linha, sizeof(linha), deadline);
        if (recebido == -1) {
            finalizar("TEMPO ESGOTADO! A bomba explodiu.", 1); break;
        }
        if (recebido == 0) {
            finalizar("Um jogador desconectou. Partida encerrada.", 1); break;
        }
        linha[120] = '\0';
        for (size_t k = 0; linha[k]; k++)
            if (linha[k] == '|') linha[k] = '/';
        snprintf(mensagem, sizeof(mensagem), "HINT|Pista do Analista: %s", linha);
        enviar_linha(jogadores[1].socket, mensagem);
        enviar_linha(jogadores[0].socket, "INFO|Pista enviada ao Decifrador.");

        enviar_linha(jogadores[1].socket, "ASK|GUESS|Digite sua tentativa com exatamente 4 digitos:");
        recebido = receber_linha(jogadores[1].socket, linha, sizeof(linha), deadline);
        if (recebido == -1) {
            finalizar("TEMPO ESGOTADO! A bomba explodiu.", 1); break;
        }
        if (recebido == 0) {
            finalizar("Um jogador desconectou. Partida encerrada.", 1); break;
        }

        /* Atualização do estado compartilhado protegida pelo mutex. */
        travar();
        bomba->tentativas++;
        int tentativaNumero = bomba->tentativas;
        int correto = codigo_valido(linha) && strcmp(linha, bomba->codigo) == 0;
        if (correto) bomba->estado = 1;
        destravar();

        if (correto) {
            finalizar("BOMBA DESARMADA! Os dois jogadores venceram.", 1);
            break;
        }

        int acertos = 0;
        travar();
        if (codigo_valido(linha)) {
            for (int k = 0; k < 4; k++)
                if (linha[k] == bomba->codigo[k]) acertos++;
        }
        destravar();

        snprintf(mensagem, sizeof(mensagem), "RESULT|Tentativa %d/%d: %d digito(s) na posicao correta.",
                 tentativaNumero, MAX_TENTATIVAS, acertos);
        enviar_todos(mensagem);

        if (tentativaNumero >= MAX_TENTATIVAS) {
            finalizar("TENTATIVAS ESGOTADAS! A bomba explodiu.", 1);
            break;
        }
    }

limpeza:
    for (int i = 0; i < CLIENTES; i++) {
        if (jogadores[i].socket != INVALID_SOCKET) {
            shutdown(jogadores[i].socket, SD_BOTH);
            closesocket(jogadores[i].socket);
        }
    }
    if (servidor != INVALID_SOCKET) closesocket(servidor);
    if (bomba) UnmapViewOfFile(bomba);
    if (hMap) CloseHandle(hMap);
    if (hMutex) CloseHandle(hMutex);
    WSACleanup();
    printf("Servidor encerrado.\n");
    return 0;
}
