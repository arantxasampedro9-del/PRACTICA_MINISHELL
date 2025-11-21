# pragma once
#include <stdlib.h>
//hola


typedef struct { // si por ejemplo mmeto ls -l /home
    char * filename; // nombre del comando a ejecutar en este caso ls
    int argc; //numero de argumentos 3
    char ** argv; //array con cada argumento
} tcommand;

typedef struct { //si por ejemplo pongo ls -l | wc -l
    int ncommands;//numero de comandos ejecutables en la linea , en este caso 2
    tcommand * commands; //array con los comandos completos [0]= ls -l
    char * redirect_input;//guarda el archivo del que se redirige la entrada con < sort < datos.txt guardaria datos.txt
    char * redirect_output; //lo mismo que el anterior pero para salida: >
    char * redirect_error;//para redirecciones que sean de errores: 2>
    int background; // 0 si se ejecuta en foreground, 1 si termina en &
} tline;

tline *tokenize(char *str); // esta funcion que esta ne la biblioteca pasa una linea de entrada a la estructura anterior
