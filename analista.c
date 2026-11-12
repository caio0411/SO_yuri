/*
 * Processo 2 - "Analista"
 *
 * - Gera um arquivo com seu PID e dorme (SIGSTOP).
 * - Quando acordado (SIGCONT, enviado pelo Atendente):
 *     bloqueia o LNG (sem_block), imprime no maximo os 10 primeiros PIDs,
 *     apaga esses 10 do arquivo, desbloqueia o LNG e dorme de novo.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <semaphore.h>
#include <fcntl.h>

#define ARQ_LNG "lng.txt"
#define ARQ_PID_ANALISTA "analista_pid.txt"

int main(void)
{
    /* Gera arquivo com seu PID */
    FILE *f = fopen(ARQ_PID_ANALISTA, "w");
    fprintf(f, "%d\n", getpid());
    fclose(f);

    sem_t *sem_block = sem_open("/sem_block", O_CREAT, 0644, 1);

    while (1) {
        raise(SIGSTOP); /* dorme ate o Atendente mandar SIGCONT */

        sem_wait(sem_block); /* bloqueia o LNG */

        f = fopen(ARQ_LNG, "r");
        if (f != NULL) {
            /* Le e imprime no maximo 10 PIDs */
            int pids[10], lidos = 0;
            while (lidos < 10 && fscanf(f, "%d", &pids[lidos]) == 1)
                lidos++;
            if (lidos > 0) {
                printf("[Analista] PIDs atendidos:");
                for (int i = 0; i < lidos; i++)
                    printf(" %d", pids[i]);
                printf("\n");
                fflush(stdout);
            }

            /* Guarda o resto do arquivo (o que nao foi impresso) */
            long pos = ftell(f);
            fseek(f, 0, SEEK_END);
            long fim = ftell(f);
            fseek(f, pos, SEEK_SET);
            char *resto = malloc(fim - pos + 1);
            size_t tam = fread(resto, 1, fim - pos, f);
            fclose(f);

            /* Reescreve o arquivo so com o resto (apaga os que foram impressos) */
            f = fopen(ARQ_LNG, "w");
            fwrite(resto, 1, tam, f);
            fclose(f);
            free(resto);
        }

        sem_post(sem_block); /* desbloqueia o LNG */
    }
    return 0;
}
