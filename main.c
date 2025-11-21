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
        if (line->ncommands == 1 && !line->background) {
            
            pid_t pid = fork();

            if (pid < 0) {
                perror("fork");
                continue;
            }

            if (pid == 0) {
                // Hijo ejecuta
                execvp(line->commands[0].filename,
                       line->commands[0].argv);

                // Si exec falla
                perror("execvp");
                exit(1);
            }

            // Padre espera
            waitpid(pid, NULL, 0);
        }
        printf("==> "); 
    }
    return 0;

}