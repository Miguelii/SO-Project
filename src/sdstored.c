#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <poll.h>

// SERVER

// $ ./bin/sdstored etc/sdstored.conf bin/sdstore-transf

typedef struct queue {
    char **line; //Comando em espera.
    int filled; //Inidice da cauda.
    int pos; //Proximo processo em execução.
    int *pri; //prioridade
} Queue;

void term_handler(int signum);
void push(Queue *q, int length);
void shiftQueue(Queue *q,int n);
void freeSlots(char *arg);
int check_disponibilidade (char *command);

Queue *initQueue () {
    Queue *fila = calloc(1, sizeof(struct queue));
    fila->line = (char **) calloc(100, sizeof(char *));
    fila->pri = (int *) calloc(20, sizeof(int));
    fila->pos = 0;
    fila->filled = -1;
    return fila;
}

int canQ (Queue *q) {
    if (q->filled >= 0 && q->pos <= q->filled) {
        if (check_disponibilidade(q->line[q->pos])) return 1;
    }
    return 0;
}

void push(Queue *q, int length) {

  
  for (int i = 0; i < length; i++) {     
    for (int j = i+1; j < length; j++) {     
       if(q->pri[i] < q->pri[j]) { 
         
          //Swap no array dos comandos
          char **temp;
          temp = q->line[i];
          q->line[i] = q->line[j];
          q->line[j] = temp;
          
          //Swap no array das prioridades
          int *auxtemp;
          auxtemp = q->pri[i];    
          q->pri[i] = q->pri[j];    
          q->pri[j] = auxtemp;    
       }     
    }     
  } 
}

void shiftQueue(Queue *q,int n) {
    //Shift para esq no array das prioridades
    for(int i=0;i<n-1;i++){
        q->pri[i]=q->pri[i+1];
    }
    q->pri[n-1]=0;
    
    //Shift para esq no array dos comandos
    for(int i=0;i<n-1;i++){
        q->line[i]=q->line[i+1];
    }
    q->line[n-1]=NULL;
}

//Contem nome dos executaveis de cada proc-file
int maxnop, maxbcompress, maxbdecompress, maxgcompress, maxgdecompress, maxencrypt, maxdecrypt;
int nop_cur, bcompress_cur, bdecompress_cur, gcompress_cur, gdecompress_cur, encrypt_cur, decrypt_cur;

int nProcesses = 0; //N de processos ativos
char *inProcess[1024]; //Processos em execução

char *dir; //Diretoria dos executáveis dos filtros.

void sigint_handler (int signum) {
    int status;
    pid_t pid;  
    while((pid = waitpid(-1, &status, WNOHANG)) > 0) wait(NULL); //Termina todos os filhos.

    //Remove os ficheiros temporários criados pelos named pipes
    write(1, "\nA terminar servidor.\n", strlen("\nA terminar servidor.\n"));
    if (unlink("/tmp/main") == -1) {
        perror("[tmp/main] Erro ao eliminar ficheiro temporário");
        _exit(-1);
    }

    _exit(0);
}

//Handler do sinal SIGCHLD
void sigchld_handler(int signum) {
    char *tok = strdup(inProcess[--nProcesses]); //Guarda o comando (Alterações necessárias aqui)
    strsep(&tok, " ");
    strsep(&tok, " ");
    char *resto = strsep(&tok, "\n");
    freeSlots(resto); //Coloca os filtros como novamente disponíveis.
    free(tok);
}

void term_handler(int signum) {
    int status;
    pid_t pid;
    while((pid = waitpid(-1, &status, WNOHANG)) > 0) wait(NULL); //Termina todos os filhos.

    unlink("/tmp/main");
    if (unlink("/tmp/main") == -1) {
        perror("[tmp main] Erro ao eliminar ficheiro temporário");
        _exit(-1);
    }

    write(1, "\nA terminar servidor.\n", strlen("\nA terminar servidor.\n"));
    _exit(0);
}

//Atualiza informação sobre filtros em uso.
 void updateSlots(char *arg) {
    char *dup = strdup(arg);
    char *tok;
    //Dependendo do comando inserido, atualiza as variáveis correspondentes aos filtros em uso.
    while((tok = strsep(&dup, " "))) {
        if (!strcmp(tok, "nop")) nop_cur++;
        if (!strcmp(tok, "bcompress")) bcompress_cur++;
        if (!strcmp(tok, "bdecompress")) bdecompress_cur++;
        if (!strcmp(tok, "gcompress")) gcompress_cur++;
        if (!strcmp(tok, "gdecompress")) gdecompress_cur++;
        if (!strcmp(tok, "encrypt")) encrypt_cur++;
        if (!strcmp(tok, "decrypt")) decrypt_cur++;
    }
    free(dup);
}

//Atualiza informação sobre os filtros em uso
void freeSlots(char *arg) {
    char *dup = strdup(arg);
    char *tok;
    //Funcionamento igual ao updateSlots() mas decrementa em vez de incrementar.
    while((tok = strsep(&dup, " "))) {
        if (!strcmp(tok, "nop")) nop_cur--;
        if (!strcmp(tok, "bcompress")) bcompress_cur--;
        if (!strcmp(tok, "bdecompress")) bdecompress_cur--;
        if (!strcmp(tok, "gcompress")) gcompress_cur--;
        if (!strcmp(tok, "gdecompress")) gdecompress_cur--;
        if (!strcmp(tok, "encrypt")) encrypt_cur--;
        if (!strcmp(tok, "decrypt")) decrypt_cur--;
    }
    free(dup);
}

//Retorna 1 se tivermos filtros disponiveis para executar a transformação.
int check_disponibilidade (char *command) {
    char *comando = strdup(command);
    char *found;
    for (int i = 0; i < 2; i++) {
        found = strsep(&comando, " ");
    }
    found = strsep(&comando, " ");
    //Basta um filtro não estar disponível e a função retorna 0 (0 -> não há disponibilidade para execução do comando).
    do {
        if (!strcmp(found, "nop")) if (nop_cur >= maxnop) return 0;
        if (!strcmp(found, "bcompress")) if (bcompress_cur >= maxbcompress) return 0;
        if (!strcmp(found, "bdecompress")) if (bdecompress_cur >= maxbdecompress) return 0;
        if (!strcmp(found, "gcompress")) if (gcompress_cur >= maxgcompress) return 0;
        if (!strcmp(found, "gdecompress")) if (gdecompress_cur >= maxgdecompress) return 0;
        if (!strcmp(found, "encrypt")) if (encrypt_cur >= maxencrypt) return 0;
        if (!strcmp(found, "decrypt")) if (decrypt_cur >= maxdecrypt) return 0;
    } while ((found = strsep(&comando, " ")) != NULL);
    free(comando);
    return 1;
}

// Pega no array resto e substitui as transformaçoes para as respetivas diretorias
char **setArgs(char *resto) {
    int current = 0;
    char res[50];
    res[0] = 0;
    
    char **argumentos = (char **) calloc(100, sizeof(char *));
    
    char *dup = strdup(resto);
    char *tok;

    while((tok = strsep(&dup, " "))) {
        if (!strcmp(tok, "bcompress")) {
        strcat(res, dir);
        strcat(res, "bcompress");
        argumentos[current++] = strdup(res);
        }
        if (!strcmp(tok, "encrypt")) {
        strcat(res, dir);
        strcat(res, "encrypt");
        argumentos[current++] = strdup(res);
        }
        if (!strcmp(tok, "nop")) {
        strcat(res, dir);
        strcat(res, "nop");
        argumentos[current++] = strdup(res);
        }
        if (!strcmp(tok, "decrypt")) {
        strcat(res, dir);
        strcat(res, "decrypt");
        argumentos[current++] = strdup(res);
        }
        if (!strcmp(tok, "bdecompress")) {
        strcat(res, dir);
        strcat(res, "bdecompress");
        argumentos[current++] = strdup(res);
        }
        if (!strcmp(tok, "gcompress")) {
        strcat(res, dir);
        strcat(res, "gcompress");
        argumentos[current++] = strdup(res);
        }
        if (!strcmp(tok, "gdecompress")) {
        strcat(res, dir);
        strcat(res, "gdecompress");
        argumentos[current++] = strdup(res);
        }
        memset(res, 0, sizeof res);
    }
    free(dup);
    
    argumentos[current] = NULL;

    return argumentos;
}

void execs(int input, int output, char ** argumentos) {
    int i = 0;
    int pip[2];

    while(argumentos[i]!=NULL) {
        if (i != 0) {
            dup2(pip[0],0);
            close(pip[0]);
        } else {
            dup2(input,0);
            close(input);
        }

        if (argumentos[i+1] == NULL) {
            dup2(output,1);
            close(output);
        }
        else {
            if (pipe(pip) == 0) {
                dup2(pip[1],1);
                close(pip[1]);
            } else {
                perror("Pipe");
                _exit(-1);
            }
        }

        pid_t f;
        f = fork();
        if(f == -1) {
            perror("Erro fork execs");
            _exit(-1);
        } else if (f == 0) {
            char *executavel = malloc(sizeof(char) * 1024);
            strcpy(executavel, argumentos[i]);
            execlp(executavel, executavel, NULL);
            perror("Exec");
            _exit(-1);

            //execvp(argumentos[i], &argumentos[i]
        }

        i++;
    }
}


int executaProc(char *comando) {
    char *found;
    char *args = strdup(comando);
    //found = strsep(&args, " ");
    int status;

    char *input = strsep(&args, " "); //Guarda o nome e path do ficheiro de input.
    char *output = strsep(&args, " "); //Guarda nome e path do ficheiro de output.
    char *resto = strsep(&args, "\n"); //Guarda os filtros pedidos pelo utilizador.

    inProcess[nProcesses++] = strdup(comando);
    updateSlots(resto);

    for (int i = 0; i < nProcesses; i++) {
        printf("[DEBUG Task #%d: %s]\n", i+1, inProcess[i]);
    }
    
    char **argumentos = setArgs(resto);

    //debug
    for(int index = 0; argumentos[index]!=NULL; index++) printf("[DEBUG - Args] %s \n",argumentos[index]);
    printf("------ FIM ARGS ------\n");

    pid_t pid;
    pid = fork();

    if(pid == 0) {
        int input_f;
        input_f = open(input, O_RDONLY);
        if(input_f == -1) {
            perror("Erro no open input");
            return -1;
        }

        int output_f;
        output_f = open(output, O_CREAT | O_TRUNC | O_WRONLY, 0666);
        if(output_f == -1) {
            perror("Erro no open output");
            return -1;
        }
        
        execs(input_f,output_f,argumentos);
        _exit(0);
    }
    else {
        wait(&status);
    }
    free(args);

    return 0;
}



int main(int argc, char *argv[]) {

    if(argc < 3) {
        perror("Falta argumentos ");
        return 0;
    }
    if(argc > 3) {
        perror("Muitos argumentos ");
        return 0;
    }

    maxnop = maxbcompress = maxbdecompress = maxgcompress = maxgdecompress = maxencrypt = maxdecrypt = 0;
    nop_cur = bcompress_cur = bdecompress_cur = gcompress_cur = gdecompress_cur = encrypt_cur = decrypt_cur = 0;

    char buffer[1024];
    int n;
    int fd_conf = open(argv[1],O_RDONLY);
    if(fd_conf == -1) perror("Erro no .conf");

    while((n = read(fd_conf,buffer,sizeof(buffer))) > 0) {
        char *token = strtok(buffer, "\n");  // breaks buffer into a series of tokens using the delimiter \n
        do {
            char *aux = strdup(token); //copia a string token para aux
            char *found = strsep(&aux, " "); // seleciona a string de aux até ao separador " "

            if(strcmp(found,"nop") == 0) {
                maxnop = atoi(strsep(&aux, "\n"));
            }

            else if(strcmp(found,"bcompress") == 0) {
                maxbcompress = atoi(strsep(&aux, "\n"));
            }        

            else if(strcmp(found,"bdecompress") == 0) {
                maxbdecompress = atoi(strsep(&aux, "\n"));
            }
            
            else if(strcmp(found,"gcompress") == 0) {
                maxgcompress = atoi(strsep(&aux, "\n"));
            }  

            else if(strcmp(found,"gdecompress") == 0) {
                maxgdecompress = atoi(strsep(&aux, "\n"));
            }        

            else if(strcmp(found,"encrypt") == 0) {
                maxencrypt = atoi(strsep(&aux, "\n"));
            }
            
            else {
                maxdecrypt = atoi(strsep(&aux, "\n"));
            } 
            free(aux);
        } while((token = strtok(NULL,"\n")));
    }

    dir = strcat(strdup(argv[2]), "/");

    write(1, "Servidor iniciado com sucesso!\n", strlen("Servidor iniciado com sucesso!\n"));

    close(fd_conf);


    //-------

    Queue *q = initQueue();

    if (mkfifo("/tmp/main",0666) == -1) {
        perror("Mkfifo");
    }
    
    //Declaração dos handlers dos sinais.
    if (signal(SIGINT, sigint_handler) == SIG_ERR) {
        perror("[signal] erro da associação do signint_handler.");
        exit(-1);
    }
    if (signal(SIGCHLD, sigchld_handler) == SIG_ERR) {
        perror("[signal] erro da associação do sigchld_handler.");
        exit(-1);
    }
    if (signal(SIGTERM, term_handler) == SIG_ERR) {
        perror("[signal] erro da associação do sigterm_handler.");
        exit(-1);
    }

    //Abrir pipe
    char comando[1024];

    int pipe = open("/tmp/main", O_RDONLY);
    if(pipe == -1) perror("/tmp/main");
    
    //Setup da função poll()
    struct pollfd *pfd = calloc(1, sizeof(struct pollfd));
    pfd->fd = pipe;
    pfd->events = POLLIN;
    pfd->revents = POLLOUT;

    int leitura = 0;
    int hasPriority = -1;

    while(1) {

        if (canQ(q) == 1) { //Verifica sempre se pode executar o que esta na fila.
            //Caso de poder executar a fila, usa mesmo código do transform que esta mais em baixo.
            char *comandoQ = strdup(q->line[q->pos]);

            //Shift da queue
            shiftQueue(q,q->filled+1);
            q->filled--;
            
            executaProc(comandoQ);
        }
        //Execução bloqueada até ser lida alguma coisa no pipe. Diminui utilização de CPU. 
        else {
            poll(pfd, 1, -1); //Verifica se o pipe está disponivel para leitura
        }

        //ler pid do cliente
        char pid[50];
        int res = 0;
        while (read(pipe, pid+res,1) > 0) {
            res++;
        }
        pid[res++] = '\0';

        char pid_ler_cliente[strlen(pid)+6];
        strcpy(pid_ler_cliente, "/tmp/w");
        strcpy(pid_ler_cliente+6,pid);
        res = 0;

        int pipe_ler_cliente = open(pid_ler_cliente, O_RDONLY);

        leitura = read(pipe_ler_cliente,comando,sizeof(comando));
        //Handling de erro no caso de ocorrer algum problema na leitura.
        if(leitura == -1) perror("Erro no read");        
        
        comando[leitura] = 0;

        if(leitura > 0 && (strncmp(comando,"status",leitura) == 0)) {
            //./bin/sdstore proc-file samples/teste.txt output/output.txt nop
            char pid_escrever[strlen(pid)+6];
            strcpy(pid_escrever, "/tmp/r");
            strcpy(pid_escrever+6,pid);

            int pipe_escrever = open(pid_escrever, O_WRONLY);

            //printf("Pipe lido! \n");
            char mensagem[5000];
            char res[5000];
            res[0] = 0;

            for (int i = 0; i < nProcesses; i++) {
                sprintf(mensagem, "Task #%d: %s\n", i+1, inProcess[i]);
                strcat(res, mensagem);
            }

            sprintf(mensagem, "Transf nop: %d/%d (Running/Max) \n",nop_cur,maxnop);
            strcat(res,mensagem);
            sprintf(mensagem, "Transf bcompress: %d/%d (Running/Max) \n",bcompress_cur,maxbcompress);
            strcat(res,mensagem);
            sprintf(mensagem, "Transf bdecompress: %d/%d (Running/Max) \n",bdecompress_cur,maxbdecompress);
            strcat(res,mensagem);
            sprintf(mensagem, "Transf gcompress: %d/%d (Running/Max) \n",gcompress_cur,maxgcompress);
            strcat(res,mensagem);
            sprintf(mensagem, "Transf gdecompress: %d/%d (Running/Max) \n",gdecompress_cur,maxgdecompress);
            strcat(res,mensagem);
            sprintf(mensagem, "Transf encrypt: %d/%d (Running/Max) \n",encrypt_cur,maxencrypt);
            strcat(res,mensagem);
            sprintf(mensagem, "Transf decrypt: %d/%d (Running/Max) \n",decrypt_cur,maxdecrypt);
            strcat(res,mensagem);
            sprintf(mensagem, "pid: %d\n", getpid());
            strcat(res, mensagem);
            strcat(res,"\0");
            write(pipe_escrever,res,strlen(res)+1);
            
            close(pipe_escrever);
            
        }

        else if(leitura > 0 && (strncmp(comando,"proc-file",9) == 0)) {
            char pid_escrever[strlen(pid)+6];
            strcpy(pid_escrever, "/tmp/r");
            strcpy(pid_escrever+6,pid);

            int pipe_escrever = open(pid_escrever, O_WRONLY);

            write(pipe_escrever, "Pending...\n", strlen("Pending...\n"));

            printf("[DEBUG Comando Inicial] %s \n",comando);

            char *auxComando;
            //Verificar se o comando tem prioridade
            if(comando[10] == '-' && comando[11] == 'p') {
                hasPriority = 0;
                char *args = strdup(comando);
                strsep(&args, " ");
                strsep(&args, " ");
                strsep(&args, " ");
                auxComando = strsep(&args, "\n");
            } else {
                hasPriority = -1;
                char *args = strdup(comando);
                strsep(&args, " ");
                auxComando = strsep(&args, "\n");
            }

            printf("[DEBUG Comando Final] %s \n",auxComando);
            printf("[DEBUG Tem prioridade?] %d \n",hasPriority);

            if(check_disponibilidade(strdup(auxComando)) == 1) { //Verifica se temos filtros suficientes para executar o comando
                write(pipe_escrever, "Processing...\n", strlen("Processing...\n")); //informa o cliente que o pedido começou a ser processado.
                executaProc(auxComando);

                write(pipe_escrever, "Concluded\n", strlen("Concluded\n"));
                close(pipe_escrever);
            } else {
                //Adicionar pedido à queue
                q->line[++q->filled] = strdup(auxComando);
                
                //Atribuir a prioridade
                char *aux = strdup(comando);
                if(hasPriority == 0) {
                    q->pri[q->filled] = atoi(&aux[27]);
                } else {
                    q->pri[q->filled] = 0;
                }
                
                //Dar sort à Queue
                push(q,q->filled+1);
            }

        }
    }
}
