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
#include <sys/stat.h>// para umask

int main(void) {
    char buf[1024];
    tline * line;
    int i, j;
    pid_t pid1; 
    int descriptorEntrada;
    int descriptorSalida;
    char *directorio;
    char dirTemporal[1024];
    char *home;
    char ruta[1024];

    int tub[2]; //creamos una tuberia para tener parte de lectura y escritura
    pid_t h1, h2; //como son dos mandatos necesitamos dos procesos hijo

    int numComandos;
    int **tuberias;
    pid_t *hijos;
    char *argumento;
    int miUmask = 0022; // mascara inicial por defecto
    

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
        
            //comprueba que solo se haya pasado un mandato
            //y que el mandtao pasado sea "exit"
        //CUARTO PUNTO
        //-----exit------
        if (line->ncommands == 1 && strcmp(line->commands[0].argv[0], "exit") == 0) {
            printf("Saliendo...\n");
            exit(0);
            
        }

        
    
        // ---------- CD ----------
        //comprueba que solo se haya pasado un mandato
        //y que el mandato pasado sea "cd"
        if (line->ncommands == 1 && strcmp(line->commands[0].argv[0], "cd") == 0) {
            // Si no hay argumentos → ir a HOME , es decir si se pasa solo cd debe indicarme la ruta del home
            if (line->commands[0].argc == 1) {
                directorio = getenv("HOME"); //getenv lo que hace es obtener el valor de la variable de entorno HOME ya definida
                if (directorio == NULL) { 
                    fprintf(stderr, "No se encuentra $HOME\n");
                    continue; //para volver al bucle
                }
            } else {
                // si hay argumento es decir cd y algo mas : cd . el directorio tendra que tomar el valor de ese argumento
                directorio = line->commands[0].argv[1];
                
                if (directorio[0] == '~') { //si es cd ~/Documentos es otra froma de acceder a home: /home/fati/Documentos
                    home = getenv("HOME");
        
                    //creamos una cadena con la parte de home mas lo que venga despues
                    strcpy(dirTemporal, home);
                    strcat(dirTemporal, directorio + 1);

                    directorio = dirTemporal;
                }
            }

            if (chdir(directorio) < 0) {//si la carpeta existe en nuestro ordenador sustituye la ruta por el directorio real
                perror("cd");//error
            } else {// ya estamos situadiso en el directorio actual y valido lo gaurdamos en ruta e imprimimos
                // si si que existe entonces guarda un puntero a ruta en ruta para despues imprimirlo
                if (getcwd(ruta, sizeof(ruta)) != NULL) { //
                    printf("%s\n", ruta);
                }
            }

            printf("==> ");
            fflush(stdout);
            continue;
            
        }

        //QUINTO PUNTO
        //umask
        if (line->ncommands == 1 && strcmp(line->commands[0].argv[0], "umask") == 0) {

            if (line->commands[0].argc == 1) {
            // imprimir exactamente en formato 0XYZ
                printf("0%03o\n", miUmask);
                printf("==> ");
                fflush(stdout);
                continue;
            }

            // Hay argumento: debe ser exactamente "0XYZ"
            argumento = line->commands[0].argv[1];

            // Validación: longitud EXACTA de 4 caracteres
            if (strlen(argumento) != 4) {
                fprintf(stderr, "umask: formato inválido (use 0XYZ)\n");
                printf("==> ");
                fflush(stdout);
                continue;
            }

            // Validación: primer carácter debe ser '0'
            if (argumento[0] != '0') {
                fprintf(stderr, "umask: debe comenzar con 0\n");
                printf("==> ");
                fflush(stdout);
                continue;
            }

            // Validación: los otros tres deben ser octales (0-7)
            if (argumento[1] < '0' || argumento[1] > '7' || argumento[2] < '0' || argumento[2] > '7' ||argumento[3] < '0' || argumento[3] > '7') {

                fprintf(stderr, "umask: sólo dígitos octales 0–7\n");
                printf("==> ");
                fflush(stdout);
                continue;
            }

            // Convertir cadena octal → entero
            int nueva = strtol(argumento, NULL, 8);

            // Guardamos en nuestra umask interna
            miUmask = nueva;

            // También la aplicamos a nivel de sistema
            umask(miUmask);

            printf("==> ");
            fflush(stdout);
            continue;
        }

        //PRIMER PUNTO
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
                        dup2(descriptorEntrada, STDIN_FILENO);  //hacemos que los comandos en vez de leer algo escrito por la entrada actuen o hagan su funcion leyendo del fichero
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
                        dup2(descriptorSalida, STDOUT_FILENO); // ahora stdout escribe en ese fichero, stdout es el descriptor 1 (salida estandar de siempre), dup2 hace que destino (stdout) pase a apuntar al mismo recurso que origen (descriptorSalida)
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
        //SEGUNDO PUNTO
        }else if (line->ncommands == 2 && !line->background) { //ejecutamos mandato1 | mandato2 sin que este en background
            if (pipe(tub) < 0) { //si la creacion de la tuberia es menor que 0 es que hay error
                perror("pipe");
                continue;
            }
            //tub[1] → escritura
            //tub[0] → lectura

            h1 = fork(); //creamos el primer proceso hijo

            if (h1 < 0) { //si es menor que 0 el proceso da error
                perror("fork hijo1"); //deberiamos cambiarlo por fprintf(stdrr...)
                exit(1);
            } else if (h1 == 0) {
                //hijo 1 - ejecutamos el primer comando

                close(tub[0]); //el primer hijo no lee escribe al hijo 2 la salida del primer mandato

                // si el primer mandto tiene redireccion de entrada hay que tenerlo en cuenta y abrirlo para lectura
                if (line->redirect_input != NULL) {
                    descriptorEntrada = open(line->redirect_input, O_RDONLY);
                    if (descriptorEntrada < 0) {
                        perror("open redirect_input");
                        exit(1);
                    } else {
                        dup2(descriptorEntrada, STDIN_FILENO);
                        close(descriptorEntrada);
                    }
                }

                // Su salida estándar va a la tubería
                dup2(tub[1], STDOUT_FILENO); //copiamos la salida del comando que estara en la pantalla en la parte de escritura
                close(tub[1]);//ya usada la cerramos

                execvp(line->commands[0].filename, line->commands[0].argv);
                perror("execvp hijo1");
                exit(1);
            }

            // ===== Hijo 2: segundo mandato (lee de la tubería, escribe en stdout o fichero) =====
            h2 = fork();
            if (h2 < 0) {
                perror("fork hijo2");
                exit(2);
            } else if (h2 == 0) {
                // En el hijo2 no se usa el extremo de escritura
                close(tub[1]); //el hijo 2 lo que va a hacer es leer lo que se recibe por la entrada estandar pero a traves de la tuberia que es donde ha escrito el otro hijo que ha leido la entrada 

                // Su entrada estándar viene de la tubería
                dup2(tub[0], STDIN_FILENO);   //ahora el stdin a punta a lo mismo que el extremo de lectura de la tuberia 
                close(tub[0]);

                // Posible redirección de salida: > fichero (afecta al segundo mandato)
                if (line->redirect_output != NULL) {
                    descriptorSalida = open(line->redirect_output, O_WRONLY | O_CREAT | O_TRUNC, 0666);
                    if (descriptorSalida < 0) {
                        perror("open redirect_output");
                        exit(1);
                    } else {
                        dup2(descriptorSalida, STDOUT_FILENO);
                        close(descriptorSalida);
                    }
                }

                execvp(line->commands[1].filename, line->commands[1].argv);
                perror("execvp hijo2");
                exit(1);
            }

            // ===== Padre: cierra tubería y espera a los dos hijos =====
            close(tub[0]);
            close(tub[1]);

            waitpid(h1, NULL, 0);
            waitpid(h2, NULL, 0);
        //TERCER PUNTO
        }else if (line->ncommands > 2 && !line->background) {

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

                    // ----- 1. Redirecciones especiales -----
                    // Entrada estándar (SOLO PRIMER MANDATO)
                    if (i == 0 && line->redirect_input != NULL) {
                        descriptorEntrada = open(line->redirect_input, O_RDONLY);
                        if (descriptorEntrada < 0) { 
                            perror("open redirect_input"); 
                            exit(1);
                        }
                        dup2(descriptorEntrada, STDIN_FILENO);
                        close(descriptorEntrada);
                    }
                

                    // Salida estándar (SOLO ULTIMO MANDATO)
                    if (i == numComandos - 1 && line->redirect_output != NULL) {
                        descriptorSalida = open(line->redirect_output, O_WRONLY|O_CREAT|O_TRUNC, 0666);
                        if (descriptorSalida < 0) { 
                            perror("open redirect_output"); 
                            exit(1); 
                        }
                        dup2(descriptorSalida, STDOUT_FILENO);
                        close(descriptorSalida);
                    }

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

            // ====== PADRE ======
            // Cerrar tuberías
            //los hijos ya se fueron por execvp asiq eu aqui el àdre termina de cerrar todo lo que no necesita, esperar a que tdoos los hijos temrines y liberar la memoria usada
            
            //cierra tuberias
            for (i = 0; i < numComandos - 1; i++) {
                close(tuberias[i][0]);
                close(tuberias[i][1]);
            }

            // Esperar a TODOS los hijos, para que la minishell no imprima ==> antes de que acabe el pipeline
            for (i = 0; i < numComandos; i++) {
                waitpid(hijos[i], NULL, 0); //bloquea hasta que el hijo temrina
            }

            //liberar memoria usada
            free(tuberias);
            free(hijos);
        }
        printf("==> "); 
    }
    return 0;
    
        
    }
