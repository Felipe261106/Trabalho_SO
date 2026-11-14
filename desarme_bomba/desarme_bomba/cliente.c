#define _CRT_SECURE_NO_WARNINGS
#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <conio.h>

#pragma comment(lib, "Ws2_32.lib")

#define PORTA 54000
#define TAM 1024

#define TEMPO_PARTIDA 180

static DWORD inicioCronometro = 0;
static int cronometroIniciado = 0;

/* Inicia a contagem uma unica vez, quando o servidor anuncia o inicio da partida. */
static void iniciar_cronometro(void) {
    if (!cronometroIniciado) {
        inicioCronometro = GetTickCount();
        cronometroIniciado = 1;
    }
}

/* Mostra o tempo na primeira linha sem mover o cursor de digitacao do jogador. */
static void atualizar_exibicao_cronometro(void) {
    if (!cronometroIniciado) return;

    DWORD decorrido = (GetTickCount() - inicioCronometro) / 1000;
    LONG restante = TEMPO_PARTIDA - (LONG)decorrido;
    if (restante < 0) restante = 0;

    HANDLE console = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (console == INVALID_HANDLE_VALUE ||
        !GetConsoleScreenBufferInfo(console, &info)) {
        return;
    }

    COORD cursorOriginal = info.dwCursorPosition;
    COORD posicaoCronometro;
    posicaoCronometro.X = 0;
    posicaoCronometro.Y = info.srWindow.Top;

    char texto[80];
    int n = snprintf(texto, sizeof(texto),
                     "[BOMBA] Tempo para explodir: %02ld:%02ld                    ",
                     restante / 60, restante % 60);
    DWORD escritos = 0;

    SetConsoleCursorPosition(console, posicaoCronometro);
    WriteConsoleA(console, texto, (DWORD)n, &escritos, NULL);
    SetConsoleCursorPosition(console, cursorOriginal);
}

/*
 * Le caractere por caractere, sem eco automatico do Windows.
 * Assim o cronometro pode atualizar sem apagar ou misturar a pista/codigo digitado.
 * Retorna 1 ao pressionar Enter e 0 se o tempo acabar ou ocorrer erro.
 */
static int ler_entrada_com_cronometro(char *destino, size_t tamanho) {
    size_t pos = 0;
    DWORD ultimaAtualizacao = 0;

    if (tamanho == 0) return 0;
    destino[0] = '\0';

    printf("> ");
    fflush(stdout);

    while (1) {
        if (cronometroIniciado) {
            DWORD agora = GetTickCount();
            if (agora - ultimaAtualizacao >= 200) {
                atualizar_exibicao_cronometro();
                ultimaAtualizacao = agora;
            }

            DWORD decorrido = (agora - inicioCronometro) / 1000;
            if (decorrido >= TEMPO_PARTIDA) {
                printf("\nTempo esgotado. Aguarde o resultado do servidor.\n");
                return 0;
            }
        }

        if (_kbhit()) {
            int tecla = _getch();

            if (tecla == 0 || tecla == 224) {
                (void)_getch(); /* descarta codigo de tecla especial */
                continue;
            }

            if (tecla == '\r' || tecla == '\n') {
                destino[pos] = '\0';
                printf("\n");
                return 1;
            }

            if (tecla == '\b') {
                if (pos > 0) {
                    pos--;
                    destino[pos] = '\0';
                    printf("\b \b");
                    fflush(stdout);
                }
                continue;
            }

            if (tecla >= 32 && tecla <= 126 && pos < tamanho - 1) {
                destino[pos++] = (char)tecla;
                destino[pos] = '\0';
                putchar(tecla);
                fflush(stdout);
            }
        } else {
            Sleep(20);
        }
    }
}

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

static int receber_linha(SOCKET s, char *buffer, int tamanho) {
    int i = 0;
    while (i < tamanho - 1) {
        char c;
        int n = recv(s, &c, 1, 0);
        if (n <= 0) return 0;
        if (c == '\n') break;
        if (c != '\r') buffer[i++] = c;
    }
    buffer[i] = '\0';
    return 1;
}

static void ler_entrada(char *destino, size_t tamanho) {
    if (!fgets(destino, (int)tamanho, stdin)) {
        destino[0] = '\0';
        return;
    }
    destino[strcspn(destino, "\r\n")] = '\0';
    /* Evita caracteres que quebram o protocolo de mensagens. */
    for (size_t i = 0; destino[i]; i++) {
        if (destino[i] == '|') destino[i] = '/';
    }
}

int main(int argc, char *argv[]) {
    WSADATA wsa;
    SOCKET sock = INVALID_SOCKET;
    struct sockaddr_in endereco;
    char ip[64] = "";
    char nome[64];
    char linha[TAM];

    /* Como o jogo sera executado na mesma maquina, usamos localhost.
       Tambem e possivel informar outro IPv4 como argumento, se necessario. */
    if (argc >= 2) {
        strncpy(ip, argv[1], sizeof(ip) - 1);
        ip[sizeof(ip) - 1] = '\0';
    } else {
        strcpy(ip, "127.0.0.1");
    }

    printf("Seu nome: ");
    ler_entrada(nome, sizeof(nome));
    if (nome[0] == '\0') strcpy(nome, "Jogador");

    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        printf("Erro ao iniciar Winsock.\n"); return 1;
    }

    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        printf("Erro ao criar socket.\n"); WSACleanup(); return 1;
    }

    memset(&endereco, 0, sizeof(endereco));
    endereco.sin_family = AF_INET;
    endereco.sin_port = htons(PORTA);
    endereco.sin_addr.s_addr = inet_addr(ip);
    if (endereco.sin_addr.s_addr == INADDR_NONE) {
        printf("Endereco IPv4 invalido. Exemplo: 192.168.0.10\n");
        closesocket(sock); WSACleanup(); return 1;
    }

    printf("Conectando a %s:%d...\n", ip, PORTA);
    if (connect(sock, (struct sockaddr *)&endereco, sizeof(endereco)) == SOCKET_ERROR) {
        printf("Falha na conexao. Confira IP, servidor, rede e firewall.\n");
        closesocket(sock); WSACleanup(); return 1;
    }

    if (!receber_linha(sock, linha, sizeof(linha))) {
        printf("Servidor encerrou a conexao.\n");
        closesocket(sock); WSACleanup(); return 1;
    }
    if (strncmp(linha, "NOME|", 5) == 0) {
        if (!enviar_linha(sock, nome)) {
            closesocket(sock); WSACleanup(); return 1;
        }
    }

    printf("\nConectado. Aguarde os dois jogadores entrarem.\n");
    while (receber_linha(sock, linha, sizeof(linha))) {
        if (strncmp(linha, "ROLE|", 5) == 0) {
            char *separador = strchr(linha + 5, '|');
            if (separador) {
                *separador = '\0';
                printf("\n=== SEU PAPEL: %s ===\n%s\n", linha + 5, separador + 1);
            } else {
                printf("%s\n", linha);
            }
        } else if (strncmp(linha, "INFO|", 5) == 0) {
            printf("[INFO] %s\n", linha + 5);
            if (strstr(linha, "180 segundos") != NULL) iniciar_cronometro();
        } else if (strncmp(linha, "CLUEPRIVATE|", 12) == 0) {
            printf("\n[PISTA PRIVADA - SOMENTE ANALISTA] %s\n", linha + 12);
        } else if (strncmp(linha, "HINT|", 5) == 0) {
            printf("\n[PISTA RECEBIDA] %s\n", linha + 5);
        } else if (strncmp(linha, "RESULT|", 7) == 0) {
            printf("\n[RESULTADO] %s\n", linha + 7);
        } else if (strncmp(linha, "ASK|", 4) == 0) {
            char *tipo = linha + 4;
            char *sep = strchr(tipo, '|');
            if (!sep) {
                printf("%s\n", linha);
                continue;
            }
            *sep = '\0';
            printf("\n%s\n", sep + 1);
            char entrada[256];
            if (!ler_entrada_com_cronometro(entrada, sizeof(entrada))) {
                /* O servidor controla o prazo oficial e enviara o resultado ao expirar. */
                continue;
            }
            if (strcmp(tipo, "MESSAGE") == 0 && strlen(entrada) > 120)
                entrada[120] = '\0';
            if (!enviar_linha(sock, entrada)) {
                printf("Falha ao enviar mensagem.\n");
                break;
            }
        } else if (strncmp(linha, "FIM|", 4) == 0) {
            printf("\n========== FIM DE JOGO ==========\n%s\n", linha + 4);
            break;
        } else {
            printf("%s\n", linha);
        }
    }

    printf("\nConexao encerrada.\n");
    closesocket(sock);
    WSACleanup();
    return 0;
}
