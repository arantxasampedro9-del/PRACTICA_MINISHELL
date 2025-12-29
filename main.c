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
    FILE *fSalida;
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
// Calcula cómo repartir "total" vacunas entre los 5 centros según la demanda (esperando[]).
// - Si nadie espera (suma=0), NO reparte nada (todo 0).
// - Si hay demanda, reparte proporcionalmente.
// - Asegura que la suma del reparto sea EXACTAMENTE "total" (cuando suma>0).
static void calcularReparto(const int esperando[CENTROS], int total, int reparto[CENTROS]) {
    int i;
    int suma = 0;
    int asignado = 0;
    int resto;
    int pesos[CENTROS];//ccc

    // Copiamos pesos (demanda) y evitamos valores negativos por seguridad
    for (i = 0; i < CENTROS; i++) {
        reparto[i] = 0;
        pesos[i] = esperando[i];
        if (pesos[i] < 0) pesos[i] = 0;
        suma += pesos[i];
    }

    // ✅ Si no hay demanda, NO se entrega nada (todo 0)
    if (suma == 0) {
        return;
    }

    // Reparto proporcional (parte entera)
    for (i = 0; i < CENTROS; i++) {
        long long num = (long long)total * (long long)pesos[i];
        int q = (int)(num / (long long)suma);
        reparto[i] = q;
        asignado += q;
    }

    // Ajuste del resto: repartir 1 a 1 a los centros con más demanda
    resto = total - asignado;
    while (resto > 0) {
        int best = 0;
        int j;

        for (j = 1; j < CENTROS; j++) {
            if (pesos[j] > pesos[best]) best = j;
        }

        reparto[best]++;
        resto--;

        // Bajamos ligeramente el peso para que si hay empate largo no gane siempre el mismo
        if (pesos[best] > 0) pesos[best]--;
    }
}


void* hiloFabrica(void *arg) {// el arg es un void porque el pthread lo exige
    Fabrica *f = (Fabrica*) arg; //pasa el arg a ser fabrica 
    int fabricadas = 0; //va contando el numero de vacuna que lleva hechas la fabrica 

    while (fabricadas < f->vacunasTotales) { //la fabrica trabaja hasta que fabrica todas las vacunas que le corresponden 

        // 1️⃣ Fabricar una tanda
        int tanda = rand() % (f->maxTanda - f->minTanda + 1) + f->minTanda; 
        if (fabricadas + tanda > f->vacunasTotales){ //si las fabricadas y la tanda que le toca fabricar es mayor que las vacunas totales
            tanda = f->vacunasTotales - fabricadas; //solo fabrica las que le faltan
        }

        int tiempoFab = rand() % (f->maxTiempoFab - f->minTiempoFab + 1) + f->minTiempoFab;

        //se muestra por pantalla y tambien se guarda en el fichero de salida
        printf("Fábrica %d prepara %d vacunas\n", f->idFabrica, tanda);
        fprintf(f->datos->fSalida, "Fábrica %d prepara %d vacunas\n", f->idFabrica, tanda);

        sleep((unsigned int)tiempoFab); //el hilo se duerme mientras el tiempo de fabricacion
        fabricadas += tanda; //actualizamos las fabricadas

        // 2️⃣ Decidir cómo repartir la tanda según la demanda (esperando[])
        // Para no tener el mutex mucho rato, hacemos un "snapshot" rápido de esperando[]
        int snapshot[CENTROS];
        int reparto[CENTROS];

        pthread_mutex_lock(&f->datos->mutex); 
        for (int i = 0; i < CENTROS; i++) {
            snapshot[i] = f->datos->esperando[i];
        }
        pthread_mutex_unlock(&f->datos->mutex);

        // Calculamos fuera del mutex para no bloquear a otros hilos
        calcularReparto(snapshot, tanda, reparto);

        // 3️⃣ Repartir a cada centro
        // IMPORTANTE: no dormimos con el mutex cogido (si no, bloqueas a los habitantes)
        for (int i = 0; i < CENTROS; i++) {

            // Simula el tiempo de reparto a este centro (entre 1 y maxTiempoReparto)
            int tiempoRep = rand() % f->maxTiempoReparto + 1;
            sleep((unsigned int)tiempoRep);

            // Zona crítica: actualizar stock del centro y despertar a habitantes si hace falta
            pthread_mutex_lock(&f->datos->mutex);

            f->datos->vacunaDisponibles[i] += reparto[i]; //llegan "reparto[i]" vacunas al centro i

            // Si han llegado vacunas, despertamos a los que estén esperando en ese centro
            if (reparto[i] > 0) {
                pthread_cond_broadcast(&f->datos->hayVacunas[i]); 
            }

            pthread_mutex_unlock(&f->datos->mutex);

            // Mostrar por pantalla y guardar en fichero cuántas vacunas entrega a cada centro (como el ejemplo del enunciado)
            printf("Fábrica %d entrega %d vacunas en el centro %d\n", f->idFabrica, reparto[i], i + 1);
            fprintf(f->datos->fSalida, "Fábrica %d entrega %d vacunas en el centro %d\n", f->idFabrica, reparto[i], i + 1);
        }
    }

    printf("Fábrica %d ha fabricado todas sus vacunas\n", f->idFabrica);
    fprintf(f->datos->fSalida, "Fábrica %d ha fabricado todas sus vacunas\n", f->idFabrica);

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
    char *nombreSalida = "salida_vacunacion.txt";
    if (argc >= 3) nombreSalida = argv[2];

    FILE *fSalida = fopen(nombreSalida, "w");
    if (!fSalida) {
        perror("Error abriendo fichero de salida");
        return 1;
    }

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
    fprintf(fSalida, "VACUNACIÓN EN PANDEMIA: CONFIGURACIÓN INICIAL\n");

    printf("Habitantes: %d\n", habitantesTotales);
    fprintf(fSalida, "Habitantes: %d\n", habitantesTotales);

    printf("Centros de vacunación: %d\n", CENTROS);
    fprintf(fSalida, "Centros de vacunación: %d\n", CENTROS);

    printf("Fábricas: %d\n", 3);
    fprintf(fSalida, "Fábricas: %d\n", 3);

    printf("Vacunados por tanda: %d\n", habitantesTotales / 10);
    fprintf(fSalida, "Vacunados por tanda: %d\n", habitantesTotales / 10);

    printf("Vacunas iniciales en cada centro: %d\n", vacunasInicialesPorCentro);
    fprintf(fSalida, "Vacunas iniciales en cada centro: %d\n", vacunasInicialesPorCentro);

    printf("Vacunas totales por fábrica: %d\n", habitantesTotales / 3);
    fprintf(fSalida, "Vacunas totales por fábrica: %d\n", habitantesTotales / 3);

    printf("Mínimo número de vacunas fabricadas en cada tanda: %d\n", minVacTanda);
    fprintf(fSalida, "Mínimo número de vacunas fabricadas en cada tanda: %d\n", minVacTanda);

    printf("Máximo número de vacunas fabricadas en cada tanda: %d\n", maxVacTanda);
    fprintf(fSalida, "Máximo número de vacunas fabricadas en cada tanda: %d\n", maxVacTanda);

    printf("Tiempo mínimo de fabricación de una tanda de vacunas: %d\n", minTiempoFab);
    fprintf(fSalida, "Tiempo mínimo de fabricación de una tanda de vacunas: %d\n", minTiempoFab);

    printf("Tiempo máximo de fabricación de una tanda de vacunas: %d\n", maxTiempoFab);
    fprintf(fSalida, "Tiempo máximo de fabricación de una tanda de vacunas: %d\n", maxTiempoFab);

    printf("Tiempo máximo de reparto de vacunas a los centros: %d\n", maxTiempoReparto);
    fprintf(fSalida, "Tiempo máximo de reparto de vacunas a los centros: %d\n", maxTiempoReparto);

    printf("Tiempo máximo que un habitante tarda en ver que está citado para vacunarse: %d\n", maxTiempoReaccion);
    fprintf(fSalida, "Tiempo máximo que un habitante tarda en ver que está citado para vacunarse: %d\n", maxTiempoReaccion);

    printf("Tiempo máximo de desplazamiento del habitante al centro de vacunación: %d\n", maxTiempoDesplaz);
    fprintf(fSalida, "Tiempo máximo de desplazamiento del habitante al centro de vacunación: %d\n", maxTiempoDesplaz);

    printf("PROCESO DE VACUNACIÓN\n");
    fprintf(fSalida, "PROCESO DE VACUNACIÓN\n");

    // 3) Inicializar datos compartidos
    DatosCompartidos datos;
    datos.fSalida = fSalida;

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
    fprintf(fSalida, "Vacunación finalizada\n");
    // 7) Limpieza
    for (int i = 0; i < CENTROS; i++) {
        pthread_cond_destroy(&datos.hayVacunas[i]);
    }
    pthread_mutex_destroy(&datos.mutex);

    fclose(datos.fSalida);
    return 0;
}
