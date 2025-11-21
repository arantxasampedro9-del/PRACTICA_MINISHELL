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
    int descriptorSalida;

    
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

                // CAMBIAR ESTO A FUNCION (REDIRECCION DE SALIDA)
                if (line->redirect_output != NULL) { //redi_output guarda el nombre del fichero despues de >, si es distinto de NULL, es porque el usuario ha puesto > en la linea: por lo que hay que redirigir la salida estandar a ese archivo
                    descriptorSalida = open(line->redirect_output, O_WRONLY | O_CREAT | O_TRUNC, 0666);
                    //abre el fichero donde hay que escribir
                    //primer parametor: nombre del archivo
                    //segundo parametros es un conjunto de flags combinadas (abrir solo para escribir)(si el fichero no existe, lo crea)(si el fichero ya existe, lo deja a tamaño 0)
                    //tercer parametro: como en las flags hay O_CREAT hay que ponerlo obligatoriamente, hay qu eponer 0666 que son los permisos por defecto si se crea el archivo (o el modo que queramos)
                    if (descriptorSalida < 0) { //da error al abrir fichero
                        perror("open redirect_output");
                        exit(1);
                    }else{
                        dup2(descriptorSalida, stdout); // ahora stdout escribe en ese fichero, stdout es el descriptor 1 (salida estandar de siempre), dup2 hace que destino (stdout) pase a apuntar al mismo recurso que origen (descriptorSalida)
                        //a partir de ahora, todo lo que se escriba por la salida estandar ir al fichero abierto de descriptoSalida. cualquier printf, puts, etcc del programa no va a la temriinal, va al fichero
                        close(descriptorSalida); //ya no necesitamos descsalida tenemos el stdout que apunta al mismo sitio
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