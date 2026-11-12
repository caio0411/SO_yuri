# Trabalho 1 de Sistemas Operacionais: Atendimento de Clientes

Simulação de um balcão de atendimento em C, no Linux. Uma thread **Recepção** cria processos **Cliente** e os coloca numa fila. Uma thread **Atendente** os retira da fila e os atende. Um processo **Analista** imprime os PIDs dos clientes atendidos.

As threads não usam pthreads: são criadas com troca de contexto (`ucontext`) e alternadas por um escalonador próprio.

## Como executar

Requisitos: Linux com `gcc` e `make`. No Ubuntu/Debian, instale com `sudo apt install build-essential`.

Rode os comandos de dentro da pasta do projeto:

```bash
make                     # compila atendimento, analista e cliente
./atendimento N X        # executa
make clean               # apaga executáveis e arquivos gerados
```

- `N`: número de clientes a criar. Com `N = 0`, os clientes são gerados sem parar até você apertar **s**.
- `X`: paciência, em ms, do cliente de prioridade baixa. O de prioridade alta tem `X/2`.

Exemplos:

```bash
./atendimento 100 10     # 100 clientes, paciência de 10 ms (alta: 5 ms)
./atendimento 0 10       # clientes infinitos; aperte "s" para parar
```

Ao final, o programa imprime os clientes atendidos, os satisfeitos, a **taxa de satisfação** e o **tempo total de execução**.

`make run N=100 X=10` compila e executa de uma vez.

**No WSL (Windows):** se o projeto estiver no disco do Windows (`/mnt/c/...`), o `make` copia o código para `~/trabalho_so`, no disco nativo do Linux, e compila lá. Ele também deixa na pasta do projeto um atalho `./atendimento` que executa essa versão. Isso porque operações de arquivo em `/mnt/c` são cerca de 20 vezes mais lentas. Num Linux normal, o `make` só compila na própria pasta.

## Arquivos

| Arquivo | Descrição |
| --- | --- |
| `atendimento.c` | Processo principal: escalonador, threads Recepção e Atendente, fila e resultado final |
| `analista.c` | Processo Analista: ao ser acordado, imprime até 10 PIDs do arquivo LNG e os apaga |
| `cliente.c` | Processo Cliente fornecido pelo professor, sem alterações |
| `Makefile` | Compilação (`make`), execução (`make run`) e limpeza (`make clean`) |

Gerados durante a execução: `demanda.txt` (tempo de atendimento escrito pelo cliente), `lng.txt` (PIDs atendidos, lidos pelo Analista) e `analista_pid.txt` (PID do Analista).

## Como funciona

- **Escalonador:** round-robin cooperativo. Cada thread tem pilha e contexto próprios e devolve a CPU com `ceder()`, que faz `swapcontext` para o escalonador.
- **Recepção:** cria cada cliente com `vfork` + `exec` e espera ele escrever `demanda.txt` e parar (`SIGSTOP`). Depois sorteia a prioridade (50% cada) e o coloca na fila, que tem no máximo 100 clientes.
- **Atendente:** escolhe o cliente de prazo mais próximo (`chegada + paciência`) entre os que ainda podem sair satisfeitos. Acorda o cliente (`SIGCONT`), espera ele terminar e o `sem_atend` abrir, grava o PID no LNG protegido por `sem_block` e calcula a satisfação. A cada 10 atendimentos, acorda o Analista. Antes de terminar, acorda o Analista até o LNG ficar vazio.
- **Analista:** nasce dormindo. Ao ser acordado, trava o LNG, imprime e apaga no máximo 10 PIDs, destrava e volta a dormir.
- **Satisfação:** um cliente fica satisfeito se `(fim do atendimento − hora de chegada) ≤ paciência`. A hora de chegada é marcada quando o cliente termina de ser criado, pronto para entrar na fila.
- **Parada com "s" (N = 0):** os clientes que ainda estão na fila são descartados e não entram na taxa. Todos os PIDs já atendidos são impressos antes de o programa terminar.
