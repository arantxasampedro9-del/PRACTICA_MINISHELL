#include <stdio.h>
#include "parser.h"
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#include <string.h>
#include <limits.h>
#include <unistd.h>
#include <sys/stat.h> // para umask
#include <errno.h>


//para el jobs 
typedef struct {
    pid_t pid;
    char comando[1024];
    int id;
} trabajoBG;

void redireccionEntrada(tline *line, int i, int *descriptorEntrada);
void redireccionSalida(tline *line, int i, int numComandos, int *descriptorSalida);

int main(void) {
    tline * line;

    int descriptorEntrada;
    int descriptorSalida;

    int i, j, n;

    char buf[1024];

    int ultimo;
    pid_t pid1; 

    //cd
    char *directorio;
    char dirTemporal[1024];
    char *home;
    char ruta[1024];

    //umask
    char *argumento;
    char *final;
    mode_t miUmask; 
    long nuevaMascara;

     //jobs
    pid_t estadoProceso;

    //fg
    int id; 

    //jobs y fg
    trabajoBG *arrayTrabajos=NULL; 
    int numTrabajos = 0; //son los trabajos del background

    //para la parte de control de comandos
    int numComandos;
    char comandoNuevo[1024];
    int tub[2]; 
    pid_t hijo1, hijo2; //como son dos comandos necesitamos dos procesos hijo
    int **tuberias;
    pid_t *hijos;



    signal(SIGINT, SIG_IGN);

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
        //-----exit------       
        //comprueba que solo se haya pasado un mandato y que el mandtao pasado sea "exit"
        if (line->ncommands == 1 && strcmp(line->commands[0].argv[0], "exit") == 0) {
            printf("Saliendo de la shell ...\n");
            exit(0);
            
        }
    
        // ----------cd----------
        //comprueba que solo se haya pasado un mandato y que el mandato pasado sea "cd"
        if (line->ncommands == 1 && strcmp(line->commands[0].argv[0], "cd") == 0) {
            // Si no hay argumentos va a HOME
            if (line->commands[0].argc == 1) {
                directorio = getenv("HOME"); 
                if (directorio == NULL) { 
                    fprintf(stderr, "$HOME no existe\n");
                    continue; 
                }
            } else {
                // si hay argumento el directorio tendra que tomar el valor de ese argumento
                directorio = line->commands[0].argv[1]; 
                //comprueba si el primer caracter del argumento es ~ para asi convertirlo  de ~/Documentos a /home/usuario/Documentos
                if (directorio[0] == '~') { 
                    home = getenv("HOME"); 
                    //construye ruta completa
                    strcpy(dirTemporal, home);
                    strcat(dirTemporal, directorio + 1);
                    directorio = dirTemporal; 
                }
            }
            //si la carpeta existe en nuestro ordenador entra en el if y ya estamos dentro del directorio
            if (chdir(directorio) == 0) {
                if (getcwd(ruta, sizeof(ruta)) != NULL) { 
                    printf("%s\n", ruta);
                }
            } else {
                fprintf(stderr, "cd: No se encontro el directorio: %s \n", directorio);
            }

            printf("==> ");
            fflush(stdout);
            continue;
            
        }

        //--------umask------
        //comprueba que solo se haya pasado un mandato y que el mandato pasado sea "umask"
        if (line->ncommands == 1 && strcmp(line->commands[0].argv[0], "umask") == 0) {
            if (line->commands[0].argc == 1) {//si el usuario escribe simplemente umask imprimimos el que este guardado, si nos ponen algo mas es porque quieren cambiarlo a una nueva mascara
                miUmask = umask(0); 
                umask(miUmask);
                printf("0%03o\n", miUmask); 
                printf("==> ");
                fflush(stdout);
                continue;
            }
            argumento = line->commands[0].argv[1];
            //errno=0; //ponemos errno a 0 para ver si luego strtol da error
            nuevaMascara = strtol(argumento, NULL, 8);

            if (nuevaMascara < 0 ) {  //errno != 0 || end == argumento || *end != '\0' || v < 0 || v > 0777
                fprintf(stderr, "umask: valor invalido\n");
                printf("==> ");
                fflush(stdout);
                continue;
            }
            miUmask = (mode_t) nuevaMascara; // Guardamos en nuestra umask interna
            umask(miUmask); // Guardamos en nuestra umask interna  
            printf("==> ");
            fflush(stdout);
            continue;
        }

        //--------jobs------
        //comprueba que solo se haya pasado un mandato y que el mandato pasado sea "jobs"
        if (line->ncommands == 1 && strcmp(line->commands[0].argv[0], "jobs") == 0) {
            // Primero: limpiar los procesos que ya hayan terminado
            i = 0;
            while (i < numTrabajos) { //recorremos los procesos que hay en backgrouund
                estadoProceso = waitpid(arrayTrabajos[i].pid, NULL, WNOHANG); //creamos un proceso para analizar como esta cada proceso/hijo
                //si el estadoProceso toma el valor -1 o el del pid debemos borrarlo de la lista
                if (estadoProceso == arrayTrabajos[i].pid || estadoProceso == -1) {
                    for (j = i; j < numTrabajos - 1; j++) {
                        arrayTrabajos[j] = arrayTrabajos[j + 1];
                    }
                    numTrabajos--;
                    continue; 
                }
                i++;
            }

            for (i = 0; i < numTrabajos; i++) { //recorre el numero de procesos que estan guardados en la estructura
                printf("[%d]+  Running  %s &\n", i+1, arrayTrabajos[i].comando);
            }

                printf("==> ");
                fflush(stdout);
                continue;
        }

        //--------fg------
        //comprueba que solo se haya pasado un mandato y que el mandato pasado sea "fg"
        if (line->ncommands == 1 && strcmp(line->commands[0].argv[0], "fg") == 0) {
            if (line->commands[0].argc == 1) {  //// Si "fg" no tiene argumentos implica que traiga a foreground el último trabajo
                if (numTrabajos == 0) { //si no hay trabajos no va a haber ultimo por tanto error
                    fprintf(stderr, "fg: no hay ningun trabajo no puedo traer nada a foreground\n");
                    printf("==> ");
                    fflush(stdout);
                    continue;
                }
                id = numTrabajos - 1; 
            } else { //en el caso de que me indique que trabajo quiere que traiga a foreground
                id = atoi(line->commands[0].argv[1]) - 1;
                if (id < 0 || id >= numTrabajos) { 
                    fprintf(stderr, "fg: el identificador %d no es correcto\n", id);
                    printf("==> ");
                    fflush(stdout);
                    continue;
                }
            }
            printf("%s\n", arrayTrabajos[id].comando);
            waitpid(arrayTrabajos[id].pid, NULL, 0); //la shell se bloquea y espera a que termine el trabajo creado con ese id
            //cuando el proceso termina debemos eliminarlo de la lista
            for (i = id; i < numTrabajos - 1; i++) {
                arrayTrabajos[i] = arrayTrabajos[i+1]; 
            }
            numTrabajos--;
            printf("==> ");
            fflush(stdout);
            continue;
        }

        //Ser capaz de reconocer y ejecutar tanto en foreground como en background líneas con 1 o más mandatos
        //con sus argumentos, enlazados con ‘|’, con redirección de entrada estándar desde archivo y redirección de salida a archivo
        if (line->ncommands >= 1) {
            numComandos=line->ncommands;  
            tuberias = malloc((numComandos - 1) * sizeof(int*)); //reserva de memoria para las tuberias
            if (tuberias == NULL) {
                fprintf(stderr, "No se ha podido reservar memoria para las tuberias");
                exit(1);
            }
            // se crea cada tuberia como un array de dos enteros para los extremos 
            // tuberias[i][0] --> extremos de lectura
            // tuberias[i][1] --> extremos de escritura
            for (i = 0; i < numComandos - 1; i++) {
                tuberias[i] = malloc(2 * sizeof(int)); 
                    if (tuberias[i] == NULL) {
                        fprintf(stderr, "No se han podido crear los extremos de la tubería");
                        exit(1);
                    }
            }     
            // se reserva memoria para los hijos
            hijos = malloc(numComandos * sizeof(pid_t));   
            if (hijos == NULL) {
                fprintf(stderr, "Error al reservar memoria para los hijos");
                exit(1);
            }
            //Crear las tuberías necesarias 
            for (i = 0; i < numComandos - 1; i++) {
                if (pipe(tuberias[i]) < 0) {  
                    fprintf(stderr, "Error al crear la tuberia");
                     exit(1);
                }
            }
            //------hijos------
            for (i = 0; i< numComandos; i++) { // se crea un hijo por comando
                hijos[i] = fork();
                if (hijos[i] < 0) {
                    perror("fork");
                    exit(1);
                }else if (hijos[i] == 0) {
                    //  HIJO i
                    //cada hijo: 
                    //lee de la tubería anterior (menos el primero)
                    //escribe en la tubería siguiente (menos el último)
                    //hacer las redirecciones pertinentes 
                    //ejecutar su comando con execvp

                    if (!line->background){ //si no esta en background, con la señal ctrl+c vuelve a imprimir => en lugar de salirse
                        signal(SIGINT, SIG_DFL);
                    } 
                    
                    redireccionEntrada(line, i, &descriptorEntrada);
                    redireccionSalida(line, i, numComandos, &descriptorSalida);
                    if (i > 0) {
                        dup2(tuberias[i-1][0], STDIN_FILENO); 
                    }
                    if (i < numComandos - 1) {
                        dup2(tuberias[i][1], STDOUT_FILENO);
                    }
                    //Cerrar TODAS las tuberías del hijo que se esta ejecutando
                    for (j = 0; j < numComandos - 1; j++) {
                        close(tuberias[j][0]);
                        close(tuberias[j][1]);
                    }
                    execvp(line->commands[i].filename, line->commands[i].argv);
                    fprintf(stderr, "No se ha podido ejecutar el comando");
                    exit(1);
                }
            }

            // -------padre-------
            //cierra tuberias
            for (i = 0; i < numComandos - 1; i++) {
                close(tuberias[i][0]);
                close(tuberias[i][1]);
            }
            //foreground
            if (!line->background) {
                for (i = 0; i < numComandos; i++) {
                    waitpid(hijos[i], NULL, 0);
                }
            } else { //background
                arrayTrabajos = realloc(arrayTrabajos, (numTrabajos + 1) * sizeof(trabajoBG)); 
                //como puede haber ya huecos del array creados de otros comandos en backround es necesario que en cada iteracion de uno de ellos redimensionemos la memoria porque con malloc podria noc caber
                //esto es ara genera un nuevo trabajo en el array de trabajos y debe ser manteniendo el numero
                //de elementos que habia antes + 1 es decir: numTrabajos +1 del stipo TrabajosBG que es nuestra estructura

                if (arrayTrabajos == NULL) {
                    fprintf(stderr, "Error de reasignacion de memoria");
                    exit(1);
                }

                arrayTrabajos[numTrabajos].pid = hijos[numComandos - 1]; 
                
                strcpy(comandoNuevo, ""); //creamos una cadena vacia
                n = 0; //indice para recorrer todas las palabras del comando ls  -l es argv[0] -l es argv[1] por ejemplo
                ultimo = line->ncommands - 1; // calcula el índice del último comando en la línea EL QUE NOS INTERESA

                while (line->commands[ultimo].argv[n] != NULL) { //siempre querremos guardra el ultimo comando porque es el que va a background
                    strcat(comandoNuevo, line->commands[ultimo].argv[n]); //recorremos todos los caracteres del ultimo argumento para poder guardarlo en una cadena de forma limpi hasta llegar al final de el
                    // si argv = {"ls", "-l", "/tmp", NULL} → comandoNuevo se convierte en "ls -l /tmp "
                    strcat(comandoNuevo, " ");
                    n++;
                }

                strncpy(arrayTrabajos[numTrabajos].comando, comandoNuevo, sizeof(arrayTrabajos[numTrabajos].comando));
                // copia comandoNuevo dentro del campo comando del struct del job, la parte del final intenta evitar overflow si comandoNuevo es grande
                //sizeof(arrayTrabajos[numTrabajos].comando) nos da el tamaño maximo que puede tener el campo comando de la estructura trabajoBG
                arrayTrabajos[numTrabajos].id = numTrabajos + 1; //como el numTrabajos empieza desde 0 el id para el nuevo es uno mas
                numTrabajos++;

                printf("[%d] %d\n", numTrabajos, hijos[numComandos - 1]);
            }

            //liberar memoria usada
            for (i = 0; i < numComandos - 1; i++) {
                free(tuberias[i]);
            }
            free(tuberias);


            free(hijos);
            printf("==> ");
            fflush(stdout);
            continue;
        }
        printf("==> "); 
    }
    free(arrayTrabajos);
    return 0;
    
        
    }

void redireccionEntrada(tline *line, int i, int *descriptorEntrada) {
    if (i == 0 && line->redirect_input != NULL) {

        *descriptorEntrada = open(line->redirect_input, O_RDONLY);
        if (*descriptorEntrada < 0) { 
            fprintf(stderr, "Error al abrir el arhcivo de entrada"); 
            exit(1);
        }

        dup2(*descriptorEntrada, STDIN_FILENO);
        close(*descriptorEntrada);
    }
}

void redireccionSalida(tline *line, int i, int numComandos, int *descriptorSalida) {
    if (i == numComandos - 1 && line->redirect_output != NULL) {

        *descriptorSalida = open(line->redirect_output, O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (*descriptorSalida < 0) { 
            fprintf(stderr, "Error al escribir en el archivo de salida"); 
            exit(1); 
        }

        dup2(*descriptorSalida, STDOUT_FILENO);
        close(*descriptorSalida);
    }
}
