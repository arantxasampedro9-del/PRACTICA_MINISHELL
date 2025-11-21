#include <stdio.h>
#include "parser.h"
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>

int main(void) {
    char buf[1024];
    tline * line;
    int i,j;
    pid_t pid1;
    int descriptorEntrada;
    int fd_out;

    
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
            }else if (pid1 == 0) {
                // Hijo ejecuta, si hicieramos esto sin hijo la execvp sustitute el programa actual por el comando, perderiamos la minishell y se convertiria en ls, cat, etc. 
                //accede en parser al struct de tline que hay variable commands que es de tipo struct tcommand en el que hay variable filename
                //con el 0 se accede al primer mandato, porque ya hemos puesto el main.c antes y nos pone la => para poner los comandos 
                //execvp(nombre del programa a ejecutar (comando), lista de argumentos argv (no se pone [0] porque queremos la lista entera))
                
                //CAMBIAR A FUNCION!!!!!!!!!!!
                if (line->redirect_input != NULL) { //Si el puntero de entrada no apunta a un archivo nulo significa que apunta a un archivo yq ue hay redireccion de entrada
                    descriptorEntrada = open(line->redirect_input, O_RDONLY); 
                    //asignamos un entero a la funcion abrir para ver si lo hace correctamnete o no, EN MODO LECTURA
                    //si el entero es =3 es un identificador de un archivo
                    if (descriptorEntrada<0) {//si es menor que 0 significa que no se ha podido abrir porque alomejor no existe
                        perror("open redirect_input"); //LOS ERRORES HAY QUE VER COMO PONERLOS!!
                        exit(1);
                    }else{//si se ha abierto correctamente
                        dup2(descriptorEntrada, stdin);  //hacemos que los comandos en vez de leer algo escrito por la entrada actuen o hagan su funcion leyendo del fichero
                        close(descriptorEntrada);//siempre se cierra
                    }
                }

                // Redirección de salida estándar a archivo:  > fichero
                if (line->redirect_output != NULL) {
                    fd_out = open(line->redirect_output, O_WRONLY | O_CREAT | O_TRUNC, 0666);
                    if (fd_out < 0) {
                        perror("open redirect_output");
                        exit(1);
                    }else{
                        dup2(fd_out, STDOUT_FILENO); // ahora stdout escribe en ese fichero
                        close(fd_out);
                    }
                }
            
                execvp(line->commands[0].filename, line->commands[0].argv); 

                // Si exec falla
                perror("execvp");
                exit(1);
            }else{

            // Padre espera, espera a un hijo concreto a terminar 
                waitpid(pid1, NULL, 0);
            }
        }
        printf("==> "); 
    }
    return 0;

}