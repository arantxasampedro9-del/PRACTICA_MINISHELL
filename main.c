#include <stdio.h>
#include "parser.h"
#include <stdlib.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
//ver cambio
int main(void) {
    char buf[1024];
    tline * line;
    int i,j;
    pid_t pid1;

    printf("==> "); 
    while (fgets(buf, 1024, stdin)) {
        line = tokenize(buf);
        if (line == NULL)
            continue;
        if (line->redirect_input != NULL) {
            printf("redirección de entrada: %s\n", line->redirect_input);
        }
        if (line->redirect_output != NULL) {
            printf("redirección de salida: %s\n", line->redirect_output);
        }
        if (line->redirect_error != NULL) {
            printf("redirección de error: %s\n", line->redirect_error);
        }
        if (line->background) {
            printf("comando a ejecutarse en background\n");
        } 
        for (i=0; i<line->ncommands; i++) {
            printf("orden %d (%s):\n", i, line->commands[i].filename);
            for (j=0; j<line->commands[i].argc; j++) {
                printf("  argumento %d: %s\n", j, line->commands[i].argv[j]);
            }
        }
        //primer punto
        //line es la estructura que te devuelve el parser tline
        //line->background vale 0 → NO es en background.
        if (line->ncommands == 1 && !line->background) { //se ejecuta cuando solo hay un mandato y !0 = 1 = foreground, si el parser ve el "&" al final pone line->background a 1
            pid1 = fork(); //se crea un hijo, para duplicar el proceso, el padre es la minishell y el hijo es una copia de la minishell para luego ejecutar el comando

            if (pid1 < 0) {
                perror("fork");
                continue;
            }

            if (pid1 == 0) {
                // Hijo ejecuta, si hicieramos esto sin hijo la execvp sustitute el programa actual por el comando, perderiamos la minishell y se convertiria en ls, cat, etc. 
                //accede en parser al struct de tline que hay variable commands que es de tipo struct tcommand en el que hay variable filename
                //con el 0 se accede al primer mandato, porque ya hemos puesto el main.c antes y nos pone la => para poner los comandos 
                //execvp(nombre del programa a ejecutar (comando), lista de argumentos argv (no se pone [0] porque queremos la lista entera))
                execvp(line->commands[0].filename, line->commands[0].argv); 

                // Si exec falla
                perror("execvp");
                exit(1);
            }

            // Padre espera, espera a un hijo concreto a terminar 
            waitpid(pid1, NULL, 0);
        }
        printf("==> "); 
    }
    return 0;

}