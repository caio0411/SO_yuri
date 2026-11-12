/*
 * Processo 1 - "Atendimento"
 *
 * Uso: ./atendimento N X
 *   N = numero de clientes a gerar (0 = infinitos, para com a tecla 's')
 *   X = paciencia (ms) do cliente de prioridade baixa (alta tem X/2)
 *
 * Tem duas threads, criadas com troca de contexto (ucontext) e um
 * escalonador proprio (round-robin cooperativo):
 *   - Recepcao:  cria os processos cliente e coloca na fila
 *   - Atendente: tira da fila, acorda o cliente, grava o PID no LNG
 *                e acorda o Analista
 */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <semaphore.h>
#include <fcntl.h>
#include <termios.h>
#include <ucontext.h>
#include <sys/wait.h>

#define ARQ_DEMANDA "demanda.txt"
#define ARQ_LNG "lng.txt"
#define ARQ_PID_ANALISTA "analista_pid.txt"
#define MAX_FILA 100

/* ===================== Escalonador (threads de usuario) ===================== */

#define MAX_THREADS 4
#define TAM_PILHA (256 * 1024)

typedef struct {
    ucontext_t contexto;      /* registradores salvos da thread */
    void (*funcao)(void);     /* codigo que a thread executa */
    int terminou;
    char pilha[TAM_PILHA];    /* pilha propria da thread */
} Thread;

static Thread threads[MAX_THREADS];
static int num_threads = 0;
static int thread_atual = 0;
static ucontext_t contexto_escalonador;

/* Ponto de entrada de toda thread: roda a funcao e marca como terminada.
 * Ao retornar, o uc_link devolve o controle para o escalonador. */
static void inicio_thread(void)
{
    threads[thread_atual].funcao();
    threads[thread_atual].terminou = 1;
}

static void criar_thread(void (*funcao)(void))
{
    Thread *t = &threads[num_threads++];
    t->funcao = funcao;
    t->terminou = 0;
    getcontext(&t->contexto);
    t->contexto.uc_stack.ss_sp = t->pilha;
    t->contexto.uc_stack.ss_size = TAM_PILHA;
    t->contexto.uc_link = &contexto_escalonador;
    makecontext(&t->contexto, inicio_thread, 0);
}

/* A thread atual devolve a CPU para o escalonador (troca de contexto). */
static void ceder(void)
{
    swapcontext(&threads[thread_atual].contexto, &contexto_escalonador);
}

/* Round-robin: passa por todas as threads vivas, uma de cada vez,
 * ate que todas tenham terminado. */
static void escalonar(void)
{
    int vivas;
    do {
        vivas = 0;
        for (int i = 0; i < num_threads; i++) {
            if (threads[i].terminou)
                continue;
            vivas++;
            thread_atual = i;
            swapcontext(&contexto_escalonador, &threads[i].contexto);
        }
    } while (vivas > 0);
}

/* ============================ Dados compartilhados ============================ */

typedef struct {
    pid_t pid;
    double chegada;      /* ms desde o inicio do programa */
    int prioridade;      /* 1 = alta, 0 = baixa */
    double paciencia;    /* ms */
    int tempo_atend;     /* lido do arquivo demanda */
} Cliente;

static Cliente fila[MAX_FILA];
static int tam_fila = 0;

static int N, X;
static int parar = 0;               /* 1 quando 's' foi apertado */
static int recepcao_terminou = 0;
static int atendidos = 0, satisfeitos = 0;

static pid_t pid_analista;
static sem_t *sem_atend, *sem_block;
static struct timespec t_inicio;

static double agora_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (t.tv_sec - t_inicio.tv_sec) * 1000.0 + (t.tv_nsec - t_inicio.tv_nsec) / 1e6;
}

/* ================================== Teclado ================================== */

static struct termios termo_original;
static int termo_alterado = 0;

/* Deixa o terminal sem esperar Enter e sem bloquear na leitura. */
static void teclado_modo_imediato(void)
{
    if (!isatty(STDIN_FILENO) || tcgetattr(STDIN_FILENO, &termo_original) != 0)
        return;
    struct termios t = termo_original;
    t.c_lflag &= ~(ICANON | ECHO);
    t.c_cc[VMIN] = 0;
    t.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &t);
    termo_alterado = 1;
}

static void teclado_restaurar(void)
{
    if (termo_alterado)
        tcsetattr(STDIN_FILENO, TCSANOW, &termo_original);
}

static int apertou_s(void)
{
    char c;
    while (read(STDIN_FILENO, &c, 1) == 1)
        if (c == 's' || c == 'S')
            return 1;
    return 0;
}

/* ============================ Thread 2 - Recepcao ============================ */

static void recepcao(void)
{
    sem_atend = sem_open("/sem_atend", O_CREAT, 0644, 1);
    sem_block = sem_open("/sem_block", O_CREAT, 0644, 1);

    int criados = 0;
    while (N == 0 || criados < N) {
        if (N == 0 && apertou_s()) {
            parar = 1;
            break;
        }
        if (tam_fila == MAX_FILA) { /* fila cheia: espera abrir espaco */
            ceder();
            continue;
        }

        /* Hora de chegada: inicio da criacao do cliente (orientacao do professor) */
        double chegada = agora_ms();

        /* Cria o processo cliente. vfork nao copia a memoria do pai (o filho
         * so faz exec), entao a criacao e mais rapida e sobra mais paciencia */
        pid_t pid = vfork();
        if (pid == 0) {
            execl("./cliente", "cliente", NULL);
            perror("execl ./cliente");
            _exit(1);
        }

        /* Espera o cliente escrever a demanda e dormir (SIGSTOP) */
        int status;
        while (waitpid(pid, &status, WUNTRACED | WNOHANG) == 0)
            ceder();

        /* Le o tempo de atendimento do arquivo demanda */
        int tempo = 0;
        FILE *f = fopen(ARQ_DEMANDA, "r");
        if (f != NULL) {
            if (fscanf(f, "%d", &tempo) != 1)
                tempo = 0;
            fclose(f);
        }

        /* Coloca na fila com prioridade aleatoria (50% / 50%) */
        Cliente c;
        c.pid = pid;
        c.chegada = chegada;
        c.prioridade = rand() % 2;
        c.paciencia = c.prioridade ? X / 2.0 : X;
        c.tempo_atend = tempo;
        fila[tam_fila++] = c;

        criados++;
        ceder();
    }
    recepcao_terminou = 1;
}

/* ============================ Thread 1 - Atendente ============================ */

/* Escolhe quem atender: o cliente com o prazo (chegada + paciencia) mais
 * proximo entre os que ainda podem sair satisfeitos. Se todos ja estouraram
 * a paciencia, atende o primeiro da fila. */
static int escolher_cliente(void)
{
    double agora = agora_ms();
    int escolhido = 0;
    double menor_prazo = -1;
    for (int i = 0; i < tam_fila; i++) {
        double prazo = fila[i].chegada + fila[i].paciencia;
        if (prazo >= agora && (menor_prazo < 0 || prazo < menor_prazo)) {
            escolhido = i;
            menor_prazo = prazo;
        }
    }
    return escolhido;
}

static void remover_da_fila(int i)
{
    for (int j = i; j < tam_fila - 1; j++)
        fila[j] = fila[j + 1];
    tam_fila--;
}

static int lng_tem_dados(void)
{
    sem_wait(sem_block);
    int tem = 0;
    FILE *f = fopen(ARQ_LNG, "r");
    if (f != NULL) {
        int pid;
        tem = (fscanf(f, "%d", &pid) == 1);
        fclose(f);
    }
    sem_post(sem_block);
    return tem;
}

/* Acorda o Analista quantas vezes for preciso ate o LNG ficar vazio. */
static void esvaziar_lng(void)
{
    while (lng_tem_dados()) {
        kill(pid_analista, SIGCONT);
        waitpid(pid_analista, NULL, WUNTRACED); /* espera ele dormir de novo */
    }
}

static void atendente(void)
{
    while (!parar) {
        if (tam_fila == 0) {
            if (recepcao_terminou)
                break;
            ceder();
            continue;
        }

        /* Le na fila o proximo cliente */
        int i = escolher_cliente();
        Cliente c = fila[i];

        /* Acorda o cliente e espera ele terminar o atendimento
         * (o cliente segura o sem_atend enquanto e atendido e termina logo depois) */
        kill(c.pid, SIGCONT);
        while (waitpid(c.pid, NULL, WNOHANG) == 0)
            ceder();

        /* Espera o semaforo sem_atend abrir (o cliente o libera ao terminar) */
        while (sem_trywait(sem_atend) != 0)
            ceder();
        sem_post(sem_atend);

        /* Escreve o PID no arquivo LNG, protegido pelo sem_block */
        while (sem_trywait(sem_block) != 0)
            ceder();
        FILE *f = fopen(ARQ_LNG, "a");
        fprintf(f, "%d\n", c.pid);
        fclose(f);
        sem_post(sem_block);

        /* Calcula a satisfacao do cliente */
        if (agora_ms() - c.chegada <= c.paciencia)
            satisfeitos++;
        atendidos++;

        /* Tira o cliente da fila */
        remover_da_fila(i);

        /* Acorda o Analista a cada 10 atendimentos */
        if (atendidos % 10 == 0)
            kill(pid_analista, SIGCONT);

        ceder();
    }

    /* Antes de terminar, acorda o Analista ate imprimir todos os PIDs do LNG */
    esvaziar_lng();
}

/* ================================ Finalizacao ================================ */

int main(int argc, char *argv[])
{
    if (argc != 3) {
        fprintf(stderr, "Uso: %s N X\n", argv[0]);
        return 1;
    }
    N = atoi(argv[1]);
    X = atoi(argv[2]);
    if (N < 0 || X <= 0) {
        fprintf(stderr, "N deve ser >= 0 e X deve ser > 0\n");
        return 1;
    }

    clock_gettime(CLOCK_MONOTONIC, &t_inicio);
    srand(time(NULL) ^ getpid());

    /* Limpa restos de execucoes anteriores */
    sem_unlink("/sem_atend");
    sem_unlink("/sem_block");
    remove(ARQ_LNG);

    /* Cria o Analista e espera ele "dormir" */
    pid_t pid = fork();
    if (pid == 0) {
        execl("./analista", "analista", NULL);
        perror("execl ./analista");
        _exit(1);
    }
    int status;
    waitpid(pid, &status, WUNTRACED);
    if (!WIFSTOPPED(status)) {
        fprintf(stderr, "Falha ao iniciar o analista\n");
        return 1;
    }
    FILE *f = fopen(ARQ_PID_ANALISTA, "r");
    if (f == NULL || fscanf(f, "%d", &pid_analista) != 1)
        pid_analista = pid; /* o arquivo deveria existir; usa o PID do fork */
    if (f != NULL)
        fclose(f);

    if (N == 0) {
        teclado_modo_imediato();
        printf("Gerando clientes infinitamente. Aperte 's' para parar.\n");
        fflush(stdout);
    }

    /* Cria as threads e entrega o controle ao escalonador */
    criar_thread(recepcao);
    criar_thread(atendente);
    escalonar();

    /* Clientes que ficaram na fila (caso 's') sao descartados */
    for (int i = 0; i < tam_fila; i++) {
        kill(fila[i].pid, SIGKILL);
        waitpid(fila[i].pid, NULL, 0);
    }

    /* O Atendente ja esvaziou o LNG; encerra o Analista */
    kill(pid_analista, SIGKILL);
    waitpid(pid_analista, NULL, 0);

    double tempo_total = agora_ms();
    teclado_restaurar();

    sem_close(sem_atend);
    sem_close(sem_block);
    sem_unlink("/sem_atend");
    sem_unlink("/sem_block");

    double taxa = atendidos > 0 ? 100.0 * satisfeitos / atendidos : 0.0;
    printf("\n===== Resultado =====\n");
    printf("Clientes atendidos:  %d\n", atendidos);
    printf("Clientes satisfeitos: %d\n", satisfeitos);
    printf("Taxa de satisfacao:  %.2f%%\n", taxa);
    printf("Tempo total:         %.2f ms\n", tempo_total);
    return 0;
}
