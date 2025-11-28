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

typedef struct {
    pid_t pid;
    char comando[1024];
    int id;
} trabajoBG;

void redireccionEntrada(tline *line, int i, int *descriptorEntrada);
void redireccionSalida(tline *line, int i, int numComandos, int *descriptorSalida);

int main(void) {
    
    char buf[1024];
    tline * line;
    int i, j, n, ultimo;
    pid_t pid1; 
    int descriptorEntrada;
    int descriptorSalida;
    char *directorio;
    char dirTemporal[1024];
    char *home;
    char ruta[1024];
    char comando_limpio[1024];
    int tub[2]; 
    pid_t hijo1, hijo2; //como son dos mandatos necesitamos dos procesos hijo
    int numComandos;
    int **tuberias;
    pid_t *hijos;
    char *argumento;
    char *final;
    mode_t miUmask; 
    long nuevaMascara; //ES UN INT O UN LONG? PORQUE LUEGO CON STOL CAMBIA A INT
    pid_t estadoProceso;
    trabajoBG *arrayTrabajos=NULL; 
    int numTrabajos = 0; //son los trabajos del background
    int id;
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
                fprintf(stderr, "cd: No se encontro el directorio: %s %s\n", directorio, strerror(errno));
            }

            printf("==> ");
            fflush(stdout);
            continue;
            
        }

        //--------umask------
        //comprueba que solo se haya pasado un mandato y que el mandato pasado sea "umask"
        if (line->ncommands == 1 && strcmp(line->commands[0].argv[0], "umask") == 0) {
            if (line->commands[0].argc == 1) {//si el usuario escribe simplemente umask
                miUmask = umask(0);
                umask(miUmask);
                printf("0%03o\n", miUmask); //imprime por defecto 0022
                //%o → imprimir en octal
                //%03o → imprimir con 3 dígitos, completando con ceros si hace falta
                printf("==> ");
                fflush(stdout);
                continue;
            }

            argumento = line->commands[0].argv[1];
            errno=0;
            final = NULL;
            // Convertir cadena → entero (en este caso es octal, 8)
            nuevaMascara = strtol(argumento, NULL, 8);
            //argumento → contiene algo como "0077"
            //NULL, una variable donde strtol guardará la dirección del primer carácter que NO formó parte del número, no nos interesa asique null
            //8 es pasar en base octal 
            if (errno != 0 || final == argumento || *final != '\0' || nuevaMascara < 0 ) {

                fprintf(stderr, "umask: valor invalido\n");
                printf("==> ");
                fflush(stdout);
                continue;
            }

            // Guardamos en nuestra umask interna
            miUmask = (mode_t) nuevaMascara;

            // También la aplicamos a nivel de sistema
            umask(miUmask);  
            // de esta forma si el usuario escribe umask se podra mostrar porque ya la tenemos guardada
            //A partir de esa línea: cuando tu minishell cree nuevos archivos o directorios, se aplicarán los permisos según esa nueva umask.
            
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
            // Si "fg" no tiene argumentos implica que es el último trabajo
            if (line->commands[0].argc == 1) { //si el usuario escribe fg sin el id del proceso
                if (numTrabajos == 0) { //entonces si no hay procesos volvemos
                    fprintf(stderr, "fg: no hay trabajos\n");
                    printf("==> ");
                    fflush(stdout);
                    continue;
                }
                id = numTrabajos - 1; //si si que hay traemos a foreground el ultimo proceso mandado a background
                //Restamos 1 porque tu lista usa índices desde 0 (el usuario ve [1], [2], ...) 
            } else { //el otro caso es que ponga fg con el id del proceso
            // Caso "fg 2" por ejemplo
                id = atoi(line->commands[0].argv[1]) - 1;
                //el numero de proceso es el segundo argumento que pasa el usuario es decir  argv[1] si meto fg 2 seria el 2
                //como 2 es el argv[1] e suna cadena lo pasamos a entero para que sea el id correcto
                if (id < 0 || id >= numTrabajos) {// en estos casos no seria valido
                    fprintf(stderr, "fg: identificador inválido\n");
                    printf("==> ");
                    fflush(stdout);
                    continue;
                }
            }

            printf("%s\n", arrayTrabajos[id].comando); //imprimimos el comando que queremos traer a fg que es el numero que tenga el id

            // Esperar a que termine el proceso
            waitpid(arrayTrabajos[id].pid, NULL, 0); //la shell se bloquea y espera a que termine el proceos creado con ese id

            //cuando el proceso termina debemos eliminarlo de la lista
            for (i = id; i < numTrabajos - 1; i++) {//recorremos todos los que se mantienen
                arrayTrabajos[i] = arrayTrabajos[i+1]; //si borramos uno todos deben moverse hacia atras
            }
            numTrabajos--;//ahora hay uno menos, reducimos contador

            printf("==> ");
            fflush(stdout);
            continue;
        }

        if (line->ncommands >= 1) {

            numComandos=line->ncommands;  //es cuántos comandos separa el parser según los |, calcula cuantos comandos hay
            // (type*)malloc(n_bytes), pero en C moderno no hace falta el tipo 
            //malloc: reserva un vector de punteros, uno por tubería. tuberias[0] --> ? , tuberias[1] --> ? ...
            //Cada posición del vector todavía NO tiene memoria para los dos extremos.
            tuberias = malloc((numComandos - 1) * sizeof(int*)); //reserva de memoria para las tuberias (nº de tuberias que hay), si hay n comandos se necesitan n-1 tuberias
            if (tuberias == NULL) {
                perror("malloc tuberias");
                exit(1);
            }
        
            // aqui se reserva espacio para dos enteros que son los extremos de la tuberia
            // se crea cada tuberia como un array de dos enteros
            // tuberias[i][0] --> extremos de lectura
            // tuberias[i][1] --> extremos de escritura
            for (i = 0; i < numComandos - 1; i++) {
                tuberias[i] = malloc(2 * sizeof(int)); 
                    if (tuberias[i] == NULL) {
                        perror("malloc tuberia individual");
                        exit(1);
                    }
            }   
              
            //// se reserva memoria para los hijos, se guarda el PID de cada proceso hijo para luego hacer waitpid  
            hijos = malloc(numComandos * sizeof(pid_t));   
            if (hijos == NULL) {
                perror("malloc hijos");
                exit(1);
            }

            // ===== Crear las tuberías necesarias, las reales =====
            for (i = 0; i < numComandos - 1; i++) {
                if (pipe(tuberias[i]) < 0) {  // aqui es donde el ordenador rellena los descriptores, hace internamiente: tuberias[i][0] = fd_lectura, tuberias[i][1] = fd_escritura
                    perror("pipe");
                     exit(1);
                }
            }

            // ===== Crear procesos hijos =====
            for (i = 0; i< numComandos; i++) { // si tengo A | B | C | D son 4 coandos, entonces 4 hijos uno por cada comando
                hijos[i] = fork();
                if (hijos[i] < 0) {
                    perror("fork");
                    exit(1);
                }else if (hijos[i] == 0) {
                    //  HIJO i
                    //cada hijo debe: 
                    //leer de la tubería anterior (menos el primero)
                    //escribir en la tubería siguiente (menos el último)
                    //hacer sus redirecciones
                    //ejecutar su comando con execvp

                    if (!line->background){
                        signal(SIGINT, SIG_DFL);
                    } 
                    
                    redireccion_entrada(line, i, &descriptorEntrada);
                    redireccion_salida(line, i, numComandos, &descriptorSalida);

                    // ----- 2. Conectar pipes según la posición -----

                    // Si NO es el primer comando → su stdin viene del pipe anterior
                    //si es cmd1 lee de tuberias[0][0], si es cmd2 lee de tuberias[1][0] y asi
                    //el segundo valor indica lectura el primero el numero de tuberia
                    if (i > 0) {
                        dup2(tuberias[i-1][0], STDIN_FILENO); //lee de la tuberia 
                    }

                    // Si NO es el último comando → su stdout va al pipe siguiente
                    //si es cmd 0 escribe en tuberias[0][1], si es cmd 1 escribe en tuberias[1][1]
                    if (i < numComandos - 1) {
                        dup2(tuberias[i][1], STDOUT_FILENO);
                    }

                    // ----- 3. Cerrar TODAS las tuberías -----
                    for (j = 0; j < numComandos - 1; j++) {
                        close(tuberias[j][0]);
                        close(tuberias[j][1]);
                    }

                    // ----- 4. Exec -----
                    //ejcuta el comando real
                    execvp(line->commands[i].filename, line->commands[i].argv);
                    perror("execvp hijo");
                    exit(1);
                }
            }

            // ----- PADRE -------
            //cierra tuberias
            for (i = 0; i < numComandos - 1; i++) {
                close(tuberias[i][0]);
                close(tuberias[i][1]);
            }

            if (!line->background) {
                // -------- FOREGROUND --------
                for (i = 0; i < numComandos; i++) {
                    waitpid(hijos[i], NULL, 0);
                }
            } else {
            // -------- BACKGROUND --------
            //el pipeline es una cadena de comandos conectado con |, donde la salida de un comando pasa a ser la entrada del siguiente
                arrayTrabajos = realloc(arrayTrabajos, (numTrabajos + 1) * sizeof(trabajoBG));
                if (arrayTrabajos == NULL) {
                    perror("realloc");
                    exit(1);
                }

                arrayTrabajos[numTrabajos].pid = hijos[numComandos - 1]; //guardamos el pid del proceso en background
                //el proceso que representa el pipeline es el ultimo hijo, asi que guardas su PID para poder listarlo con jobs, traerlo con fg y controlarlo
                
                strcpy(comando_limpio, "");
                n = 0;

                ultimo = line->ncommands - 1;

                while (line->commands[ultimo].argv[n] != NULL) {
                    strcat(comando_limpio, line->commands[ultimo].argv[n]);
                    strcat(comando_limpio, " ");
                    n++;
                }


                strncpy(arrayTrabajos[numTrabajos].comando, comando_limpio, sizeof(arrayTrabajos[numTrabajos].comando));
                arrayTrabajos[numTrabajos].comando[sizeof(arrayTrabajos[numTrabajos].comando)-1] = '\0';
                arrayTrabajos[numTrabajos].id = numTrabajos + 1; //le asignamos un numero de job para poder
                numTrabajos++; //actualizamos los jobs que hay en background
 
                // Mensaje estilo bash
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
    // Entrada estándar (SOLO PRIMER MANDATO)
    if (i == 0 && line->redirect_input != NULL) {

        *descriptorEntrada = open(line->redirect_input, O_RDONLY);
        if (*descriptorEntrada < 0) { 
            perror("open redirect_input"); 
            exit(1);
        }

        dup2(*descriptorEntrada, STDIN_FILENO);
        close(*descriptorEntrada);
    }
}

void redireccionSalida(tline *line, int i, int numComandos, int *descriptorSalida) {
    // Salida estándar (SOLO ULTIMO MANDATO)
    if (i == numComandos - 1 && line->redirect_output != NULL) {

        *descriptorSalida = open(line->redirect_output, O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (*descriptorSalida < 0) { 
            perror("open redirect_output"); 
            exit(1); 
        }

        dup2(*descriptorSalida, STDOUT_FILENO);
        close(*descriptorSalida);
    }
}
