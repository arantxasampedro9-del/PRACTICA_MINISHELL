#include <stdio.h> //printf
#include <stdlib.h> //rand, malloc
#include <pthread.h>
#include <unistd.h> //sleep
#include <time.h> //time()

#define CENTROS 5 //hay 5 centros

typedef struct { //lo que comparten las fabricas y los habitantes
    int vacunaDisponibles[CENTROS]; // Vacunas disponibles por centro
    int esperando[CENTROS];    // Personas esperando en cada centro
    pthread_mutex_t mutex;     //candado para que no se pisen varios thread al leer/escribir stock y esperando
    pthread_cond_t hayVacunas[CENTROS]; //señal para despertar a personas qeu estan esperando vacunas en el centro i 
} DatosCompartidos;

typedef struct {
    int idFabrica; //numero de fabrica 
    int vacunasTotales; //vacuna que debe fabricar en total HAY QUE INICIALIZARLO?
    int minTanda; 
    int maxTanda; //rango de vacunas que produce en cada tanda
    int minTiempoFab; 
    int maxTiempoFab; //tiempo aleatorio de fabricacion por tanda
    int maxTiempoReparto; //tiempo aleatorio del reparto
    DatosCompartidos *datos; //puntero para poder tocar stock[], esperando []
} Fabrica;

typedef struct {
    int idHiloHabitante;
    int maxTiempoReaccion;
    int maxTiempoDesplazamiento;
    DatosCompartidos *datos; //puntero para poder modificar mutex, vacunas...etc
} Habitante;

void* hiloFabrica(void *arg) {// el arg es un void porque el pthread lo exige
    Fabrica *f = (Fabrica*) arg; //pasa el arg a ser fabrica 
    int fabricadas = 0; //va contando el numero de vacuna que lleva hechas la fabrica 

    while (fabricadas < f->vacunasTotales) { //la fabrica trabaja hasta que fabrica todas las vacunas que le corresponden 
        // 1️⃣ Fabricar una tanda
        int tanda = rand() % (f->maxTanda - f->minTanda + 1) + f->minTanda; //50-25+1 = 26 (porque los numeros posibles son 25, 26, ..., 50: 26 numeros) rand()%26 da un numero entre 0 y 25 le sumamos el minimo 25 y pasa de 0..25 a 25..50, tanda queda entre 25 y 50
        if (fabricadas + tanda > f->vacunasTotales){ //si las fabricadas y la tanda que le toca fabricar es mayor que las vacunas totales
            tanda = f->vacunasTotales - fabricadas; //la tanda seria vacunastotales-fabricadas, es decir solo haria ya las que falten 
        }

        int tiempo = rand() % (f->maxTiempoFab - f->minTiempoFab + 1) + f->minTiempoFab; //esto es lo mismo que antes con lo de tanda
        printf("Fábrica %d prepara %d vacunas\n", f->idFabrica, tanda);
        sleep(tiempo); //el hilo se duerme mientras el tiempo de fabricacion
        fabricadas += tanda; //aqui se actualizan las fabricadas se suma a las fabricadas las hechas en la tanda

        //Reparto de vacunas y para que sea segura se hace con threads 
        pthread_mutex_lock(&f->datos->mutex);

        int centrosConEspera = 0;
        for (int i = 0; i < CENTROS; i++)
            if (f->datos->esperando[i] > 0)
                centrosConEspera++;

        // 🔹 Antiinanición: al menos 1 vacuna a centros con espera
        for (int i = 0; i < CENTROS && tanda > 0; i++) {
            if (f->datos->esperando[i] > 0) {
                f->datos->vacunaDisponibles[i]++;
                tanda--;
                printf("Fábrica %d entrega 1 vacuna al centro %d\n", f->idFabrica, i + 1);
                pthread_cond_signal(&f->datos->hayVacunas[i]);
            }
        }

        // 🔹 Reparto del resto de forma equilibrada
        int i = 0;
        while (tanda > 0) {
            f->datos->vacunaDisponibles[i % CENTROS]++;
            printf("Fábrica %d entrega 1 vacuna al centro %d\n", f->idFabrica, (i % CENTROS) + 1);
            pthread_cond_signal(&f->datos->hayVacunas[i % CENTROS]);
            tanda--;
            i++;
        }

        pthread_mutex_unlock(&f->datos->mutex);

        // Simula el tiempo de reparto
        sleep(rand() % f->maxTiempoReparto + 1);
    }

    printf("Fábrica %d ha fabricado todas sus vacunas\n", f->idFabrica);
    pthread_exit(NULL);
}

void* hiloHabitante(void *arg) { //cada habitantes es un hilo que posee esta funcion
    Habitante *habitante = (Habitante*) arg; //identificacion de cada hulo/habitante
    //es un puntero a la estructura habitante lo que permite acceder y modificar sus datos

    //“Tiempo máximo que tarda un habitante hasta que se da cuenta que le han citado para vacunarse. 
    //El mínimo es 1, lo que hace es generar un numero aleatorio entre 1 y maxTiempoReaccion
    sleep((rand() % habitante->maxTiempoReaccion + 1));

    // Selecciona un centro según su propio interés (podría hacerlo aleatoriamente).
    int centro = rand() % CENTROS; //de entre todos los centros disponibles selecciona uno aleatoriamente
    printf("Habitante %d elige el centro %d para vacunarse\n", habitante->idHiloHabitante, centro + 1);
    //Tiempo máximo de desplazamiento del habitante al centro de vacunación
    //El mínimo es 1 lo que hace es generar un numero aleatorio entre 1 y maxTiempoDesplazamiento
    sleep((rand() % habitante->maxTiempoDesplazamiento + 1));

    //EMPIEZA EL INTENTO DE VACUNARSE - ZONA CRITICA
    //dos personas no pueden vacunarse a la vez en el mismo centro, solo un hilo puede tener el mutex a la vez
    pthread_mutex_lock(&habitante->datos->mutex); 
    //la estructura habitante tiene un dato de tipo de la estructura DatosCompartidos al acceder a datos accedemos  a la estrucura con mutex, esperando...etc

    //como ya he bloqueadoel mutex significa que el habitante esta disponible para ser vacunado por lo que aumento en 1 el numero de habitantes esperando en ese centro
    //es como ponerse a la cola para vacunarse
    habitante->datos->esperando[centro]++;

    while (habitante->datos->vacunaDisponibles[centro] == 0) { //mientras en ese centro no haya vacunas para suministrar se espera el habitante
        pthread_cond_wait(&habitante->datos->hayVacunas[centro], &habitante->datos->mutex); 
        //con esto el hilo se duerme hasta recibir una señal de que haya una vacuna y proceder a la vacunacion recuperando el mutex
    }

    //como hemos salido del while significa que  hay al menos una vacuna disponible y por tanto el paciente ha sido vacunado, SE HA GASTADO UNA VACUNA EN ESE CENTRO
    //tambien debemos quitarle de la cola de espera de ese centro porque ya ha sido vacunado
    habitante->datos->vacunaDisponibles[centro]--;
    habitante->datos->esperando[centro]--;

    printf("Habitante %d vacunado en el centro %d\n", habitante->idHiloHabitante, centro + 1);
    //notificamos que hilo concreto ha sido vacunado y en que centro, ponemos centro + 1 porque empieza en 0

    pthread_mutex_unlock(&habitante->datos->mutex); //soltamos al mutex para que otro habitante pueda acceder a la zona critica = vacunarse

    pthread_exit(NULL);
}


int main(int argc, char *argv[]) {
    srand((unsigned int)time(NULL));

    // 1) Leer fichero de entrada
    const char *nombreFichero = "entrada_vacunacion.txt";
    if (argc >= 2) nombreFichero = argv[1];

    FILE *f = fopen(nombreFichero, "r");
    if (!f) {
        perror("Error abriendo fichero de entrada");
        return 1;
    }

    Fabrica fa; 
    int habitantesTotales;
    int vacunasInicialesPorCentro;
    int minVacTanda;
    int maxVacTanda;
    int minTiempoFab, maxTiempoFab;
    int maxTiempoReparto;
    int maxTiempoReaccion;
    int maxTiempoDesplaz;

    if (fscanf(f, "%d", &habitantesTotales) != 1 ||
        fscanf(f, "%d", &vacunasInicialesPorCentro) != 1 ||
        fscanf(f, "%d", &minVacTanda) != 1 ||
        fscanf(f, "%d", &maxVacTanda) != 1 ||
        fscanf(f, "%d", &minTiempoFab) != 1 ||
        fscanf(f, "%d", &maxTiempoFab) != 1 ||
        fscanf(f, "%d", &maxTiempoReparto) != 1 ||
        fscanf(f, "%d", &maxTiempoReaccion) != 1 ||
        fscanf(f, "%d", &maxTiempoDesplaz) != 1) {
        fprintf(stderr, "Error: el fichero no tiene los 9 números requeridos.\n");
        fclose(f);
        return 1;
    }
    fclose(f);

    // 2) Mostrar configuración inicial (como el ejemplo)
    printf("VACUNACIÓN EN PANDEMIA: CONFIGURACIÓN INICIAL\n");
    printf("Habitantes: %d\n", habitantesTotales);
    printf("Centros de vacunación: %d\n", CENTROS);
    printf("Fábricas: %d\n", 3);
    printf("Vacunados por tanda: %d\n", habitantesTotales / 10);
    printf("Vacunas iniciales en cada centro: %d\n", vacunasInicialesPorCentro);
    printf("Vacunas totales por fábrica: %d\n", habitantesTotales / 3);
    printf("Mínimo número de vacunas fabricadas en cada tanda: %d\n", minVacTanda);
    printf("Máximo número de vacunas fabricadas en cada tanda: %d\n", maxVacTanda);
    printf("Tiempo mínimo de fabricación de una tanda de vacunas: %d\n", minTiempoFab);
    printf("Tiempo máximo de fabricación de una tanda de vacunas: %d\n", maxTiempoFab);
    printf("Tiempo máximo de reparto de vacunas a los centros: %d\n", maxTiempoReparto);
    printf("Tiempo máximo que un habitante tarda en ver que está citado para vacunarse: %d\n", maxTiempoReaccion);
    printf("Tiempo máximo de desplazamiento del habitante al centro de vacunación: %d\n", maxTiempoDesplaz);
    printf("PROCESO DE VACUNACIÓN\n");

    // 3) Inicializar datos compartidos
    DatosCompartidos datos;

    pthread_mutex_init(&datos.mutex, NULL);
    for (int i = 0; i < CENTROS; i++) {
        datos.vacunaDisponibles[i] = vacunasInicialesPorCentro;
        datos.esperando[i] = 0;
        pthread_cond_init(&datos.hayVacunas[i], NULL);
    }

    // 4) Crear fábricas (3 threads)
    pthread_t thFabricas[3];
    Fabrica fabricas[3];

    // Reparto de cuota exacta (por si habitantes no divisible entre 3)
    int baseCuota = habitantesTotales / 3;
    int resto = habitantesTotales % 3;

    for (int i = 0; i < 3; i++) {
        fabricas[i].idFabrica = i + 1;
        fabricas[i].vacunasTotales = baseCuota + (i < resto ? 1 : 0);
        fabricas[i].minTanda = minVacTanda;
        fabricas[i].maxTanda = maxVacTanda;
        fabricas[i].minTiempoFab = minTiempoFab;
        fabricas[i].maxTiempoFab = maxTiempoFab;
        fabricas[i].maxTiempoReparto = maxTiempoReparto;
        fabricas[i].datos = &datos;

        if (pthread_create(&thFabricas[i], NULL, hiloFabrica, &fabricas[i]) != 0) {
            perror("Error creando hilo de fábrica");
            return 1;
        }
    }

    // 5) Habitantes por tandas (10 tandas)
    int totalTandas = 10;
    int porTandaBase = habitantesTotales / totalTandas;
    int restoTandas = habitantesTotales % totalTandas;

    int idHabitante = 1;

    for (int t = 0; t < totalTandas; t++) {
        int tamTanda = porTandaBase + (t < restoTandas ? 1 : 0);

        pthread_t *thHab = (pthread_t*)malloc(sizeof(pthread_t) * (size_t)tamTanda);
        Habitante *hab = (Habitante*)malloc(sizeof(Habitante) * (size_t)tamTanda);

        if (!thHab || !hab) {
            fprintf(stderr, "Error: no hay memoria para crear la tanda.\n");
            free(thHab);
            free(hab);
            return 1;
        }

        for (int i = 0; i < tamTanda; i++) {
            hab[i].idHiloHabitante = idHabitante++;
            hab[i].maxTiempoReaccion = maxTiempoReaccion;
            hab[i].maxTiempoDesplazamiento = maxTiempoDesplaz;
            hab[i].datos = &datos;

            if (pthread_create(&thHab[i], NULL, hiloHabitante, &hab[i]) != 0) {
                perror("Error creando hilo de habitante");
                // Join de los ya creados
                for (int j = 0; j < i; j++) pthread_join(thHab[j], NULL);
                free(thHab);
                free(hab);
                return 1;
            }
        }

        // Esperar a que se vacune toda la tanda antes de llamar a la siguiente
        for (int i = 0; i < tamTanda; i++) {
            pthread_join(thHab[i], NULL);
        }

        free(thHab);
        free(hab);
    }

    // 6) Esperar a que terminen las fábricas
    for (int i = 0; i < 3; i++) {
        pthread_join(thFabricas[i], NULL);
    }

    printf("Vacunación finalizada\n");

    // 7) Limpieza
    for (int i = 0; i < CENTROS; i++) {
        pthread_cond_destroy(&datos.hayVacunas[i]);
    }
    pthread_mutex_destroy(&datos.mutex);

    return 0;
}
