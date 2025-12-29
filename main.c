#include <stdio.h> 
#include <stdlib.h> 
#include <pthread.h>
#include <unistd.h>
#include <time.h> 

#define CENTROS 5 

//lo que comparten las fabricas y los habitantes
typedef struct { 
    int vacunaDisponibles[CENTROS]; 
    int personasEnEspera[CENTROS];    
    pthread_mutex_t mutex;     //para que no se pisen varios thread al leer/escribir stock y esperando
    pthread_cond_t hayVacunas[CENTROS]; //señal para personas que están esperando vacunas en el centro i 
    FILE *fSalida;
    //para las estadísticas 
    int vacunasFabricadas[3];      
    int vacunasEntregadas[3][CENTROS]; 
    int vacunasRecibidas[CENTROS];   
    int habitantesVacunados[CENTROS];     
    // Para que las fábricas puedan terminar aunque no haya demanda 
    int totalVacunados;      
    int habitantesTotales;
} DatosGenerales;

typedef struct {
    int idFabrica; 
    int vacunasTotales;
    int minTanda; 
    int maxTanda; 
    int minTiempoFab; 
    int maxTiempoFab; 
    int maxTiempoReparto; 
    DatosGenerales *datos;
} Fabrica;

typedef struct {
    int idHiloHabitante;
    int maxTiempoReaccion;
    int maxTiempoDesplazamiento;
    DatosGenerales *datos; 
} Habitante;

//esta funcion calcula cuantas vacunas de una tanda recibe cada uno de los centros en funcion de la demanda
void calcularReparto(int personasEnEspera[CENTROS], int total, int repartoVacunas[CENTROS]);

void* hiloFabrica(void *arg) {// el arg es un void porque el pthread lo exige
    Fabrica *f = (Fabrica*) arg; //pasa el arg a ser fabrica 
    int fabricadas = 0; //va contando el numero de vacuna que lleva hechas la fabrica 

    while (fabricadas < f->vacunasTotales) { //la fabrica trabaja hasta que fabrica todas las vacunas que le corresponden 

        // 1. Fabricar una tanda
        int tanda = rand() % (f->maxTanda - f->minTanda + 1) + f->minTanda; //genera aleatoriamente cuantas vacunas produce en esa tanda
        //(f->maxTanda - f->minTanda + 1) = cantidad de valores posibles, rand() % ... da un valor entre 0 y rango-1 + f->minTanda lo desplaza al rango real [minTanda, maxTanda]
        if (fabricadas + tanda > f->vacunasTotales){ //si las fabricadas y la tanda que le toca fabricar es mayor que las vacunas totales
            tanda = f->vacunasTotales - fabricadas; //solo fabrica las que le faltan
        }

        int tiempoFab = rand() % (f->maxTiempoFab - f->minTiempoFab + 1) + f->minTiempoFab; //esto es lo mismo que con lo de tanda, pero con el tiempo de fabricacion

        //se muestra por pantalla y tambien se guarda en el fichero de salida
        printf("Fábrica %d prepara %d vacunas\n", f->idFabrica, tanda);
        fprintf(f->datos->fSalida, "Fábrica %d prepara %d vacunas\n", f->idFabrica, tanda);

        sleep((unsigned int)tiempoFab); //el hilo se duerme mientras el tiempo de fabricacion
        fabricadas += tanda; //actualizamos las fabricadas con la tanda que acaba de fabricar

        //estadistica: sumar vacunas fabricadas por esta fabrica 
        //Entras en sección crítica porque vas a modificar un dato compartido entre threads. En concreto, vas a tocar vacunasFabricadasPorFabrica[], que lo pueden tocar las 3 fábricas.
        pthread_mutex_lock(&f->datos->mutex);
        f->datos->vacunasFabricadas[f->idFabrica - 1] += tanda; //actualizas estadística global de fabricación: f->idFabrica es 1,2,3 (como lo tienes), arrays empiezan en 0, por eso -1 → posiciones 0,1,2. Suma esta tanda al total fabricado por esa fábrica.
        pthread_mutex_unlock(&f->datos->mutex);

        // 2. Decidir cómo repartir la tanda según la demanda (esperando[])
        // Para no tener el mutex mucho rato, hacemos un "snapshot" rápido de esperando[]
        int snapshot[CENTROS];
        int reparto[CENTROS];
        int sumaDemanda = 0;

        pthread_mutex_lock(&f->datos->mutex); 
        for (int i = 0; i < CENTROS; i++) {
            snapshot[i] = f->datos->personasEnEspera[i];
            if (snapshot[i] > 0) sumaDemanda += snapshot[i];
        }
        pthread_mutex_unlock(&f->datos->mutex);

        // Si hay demanda, reparto proporcional.
        // Si NO hay demanda, reparto equilibrado (para no “perder” la tanda y evitar bloqueos futuros).
        if (sumaDemanda > 0) {
            // Calculamos fuera del mutex para no bloquear a otros hilos
            calcularReparto(snapshot, tanda, reparto);
        } else {
            // Reparto equilibrado: 1 a 1 por centros (round-robin)
            for (int i = 0; i < CENTROS; i++) reparto[i] = 0;
            for (int k = 0; k < tanda; k++) {
                reparto[k % CENTROS]++; 
            }
        }

        // 3️. Repartir a cada centro
        // IMPORTANTE: no dormimos con el mutex cogido (si no, bloqueas a los habitantes)
        for (int i = 0; i < CENTROS; i++) {

            if (reparto[i] <= 0) continue; //si a este centro no le toca nada, no hay reparto (ni tiempo de reparto)

            // Simula el tiempo de reparto a este centro (entre 1 y maxTiempoReparto)
            int tiempoRep = rand() % f->maxTiempoReparto + 1;
            sleep((unsigned int)tiempoRep);

            // Zona crítica: actualizar stock del centro y estadísticas + despertar habitantes
            pthread_mutex_lock(&f->datos->mutex);

            f->datos->vacunaDisponibles[i] += reparto[i]; //llegan "reparto[i]" vacunas al centro i

            // Estadísticas: entregadas por fábrica y recibidas por centro
            f->datos->vacunasEntregadas[f->idFabrica - 1][i] += reparto[i];
            f->datos->vacunasRecibidas[i] += reparto[i];

            // Si han llegado vacunas, despertamos a los que estén esperando en ese centro
            pthread_cond_broadcast(&f->datos->hayVacunas[i]); 

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



void* hiloHabitante(void *arg);

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
        fclose(fSalida); // ✅ para no dejar el fichero de salida abierto si falla la entrada
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
        fclose(fSalida);
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
    DatosGenerales datos;
    datos.fSalida = fSalida;

    // ✅ Para que las fábricas puedan terminar correctamente
    datos.totalVacunados = 0;
    datos.habitantesTotales = habitantesTotales;

    pthread_mutex_init(&datos.mutex, NULL);
    for (int i = 0; i < CENTROS; i++) {
        datos.vacunaDisponibles[i] = vacunasInicialesPorCentro;
        datos.personasEnEspera[i] = 0;
        datos.vacunasRecibidas[i] = vacunasInicialesPorCentro; 
        datos.habitantesVacunados[i] = 0;
        pthread_cond_init(&datos.hayVacunas[i], NULL);
    }

        // Inicializar estadísticas
    for (int i = 0; i < 3; i++) {
        datos.vacunasFabricadas[i] = 0;
        for (int c = 0; c < CENTROS; c++) {
            datos.vacunasEntregadas[i][c] = 0;
        }
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
            fclose(fSalida);
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
            fclose(fSalida);
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
                fclose(fSalida);
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

    // 8) Estadística final (pantalla + fichero)
    printf("\n--- ESTADÍSTICA FINAL ---\n");
    fprintf(fSalida, "\n--- ESTADÍSTICA FINAL ---\n");

    // Por cada fábrica: fabricadas y entregadas por centro
    for (int i = 0; i < 3; i++) {
        printf("Fábrica %d ha fabricado %d vacunas\n", i + 1, datos.vacunasFabricadas[i]);
        fprintf(fSalida, "Fábrica %d ha fabricado %d vacunas\n", i + 1, datos.vacunasFabricadas[i]);

        for (int c = 0; c < CENTROS; c++) {
            printf("  Entregadas al centro %d: %d\n", c + 1, datos.vacunasEntregadas[i][c]);
            fprintf(fSalida, "  Entregadas al centro %d: %d\n", c + 1, datos.vacunasEntregadas[i][c]);
        }
    }

    // Por cada centro: recibidas, vacunados y sobrantes
    for (int c = 0; c < CENTROS; c++) {
        int sobrantes = datos.vacunaDisponibles[c];

        printf("Centro %d: recibidas=%d, vacunados=%d, sobrantes=%d\n",
               c + 1,
               datos.vacunasRecibidas[c],
               datos.habitantesVacunados[c],
               sobrantes);

        fprintf(fSalida, "Centro %d: recibidas=%d, vacunados=%d, sobrantes=%d\n",
                c + 1,
                datos.vacunasRecibidas[c],
                datos.habitantesVacunados[c],
                sobrantes);
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

//esta funcion calcula cuantas vacunas de una tanda recibe cada uno de los centros en funcion de la demanda
void calcularReparto(int personasEnEspera[CENTROS], int total, int repartoVacunas[CENTROS]) {
    int i, j; 
    int v; 
    int totalPersonasEsperando= 0; 
    int vacunasAsignadas = 0; 
    int centroMayorDemanda;
    int vacunasSobrantes; 
    int copiaPersonasEnEspera[CENTROS];//copia de personasEnEspera[] para no modificar el original
    long long num;

    // Calcular el total de personas esperando y guardarlo en total
    for (i = 0; i < CENTROS; i++) {
        repartoVacunas[i] = 0; 
        copiaPersonasEnEspera[i] = personasEnEspera[i]; 
        totalPersonasEsperando += copiaPersonasEnEspera[i]; 
    }

    if (totalPersonasEsperando == 0) {
        return; //si no hay nadie esperando en ningun centro no se reparte nada
    }

    //usamos long por si acaso hay que calcular numeros grandes, evitamos desbordamientos
    for (i = 0; i < CENTROS; i++) {
        num = (long long)total * (long long)copiaPersonasEnEspera[i]; //calcula cuantas vacunas le corresponden a ese centro en funcion de su demanda
        v = (int)(num / (long long)totalPersonasEsperando); //parte entera de la division
        repartoVacunas[i] = v; //asigna esas vacunas al centro i
        vacunasAsignadas += v; //con esto sabemos cuantas vacunas hemos asignado ya en total
    }

    vacunasSobrantes = total - vacunasAsignadas; //calculamos cuantas vacunas nos quedan por repartir
    while (vacunasSobrantes > 0) { 
        centroMayorDemanda= 0;
        
        for (j = 1; j < CENTROS; j++) { //busca el centro que mas gente tiene esperando
            if (copiaPersonasEnEspera[j] > copiaPersonasEnEspera[centroMayorDemanda]) {
                centroMayorDemanda = j;
            }
        }

        repartoVacunas[centroMayorDemanda]++; //le damos una vacuna al centro con mas demanda
        vacunasSobrantes--; //quitamos una vacuna de las que quedaban por repartir

        if (copiaPersonasEnEspera[centroMayorDemanda] > 0){
            copiaPersonasEnEspera[centroMayorDemanda]--; 
            //reparto equilibrado 
        }
    }
}

void* hiloHabitante(void *arg) { //cada habitante es un hilo que posee esta funcion
    Habitante *habitante = (Habitante*) arg; //es un puntero a la estructura habitante lo que permite acceder y modificar sus datos

    sleep((rand() % habitante->maxTiempoReaccion + 1)); //El mínimo es 1, lo que hace es generar un numero aleatorio entre 1 y maxTiempoReaccion

    int centro = rand() % CENTROS; //de entre todos los centros disponibles selecciona uno aleatoriamente
    printf("Habitante %d elige el centro %d para vacunarse\n", habitante->idHiloHabitante, centro + 1);
    fprintf(habitante->datos->fSalida, "Habitante %d elige el centro %d para vacunarse\n", habitante->idHiloHabitante, centro + 1);
   
    sleep((rand() % habitante->maxTiempoDesplazamiento + 1));  //El mínimo es 1 lo que hace es generar un numero aleatorio entre 1 y maxTiempoDesplazamiento

    //ZONA CRITICA, dos personas no pueden vacunarse a la vez en el mismo centro, solo un hilo puede tener el mutex a la vez
    pthread_mutex_lock(&habitante->datos->mutex); 
    
    habitante->datos->personasEnEspera[centro]++; //el habitante esta disponible para ser vacunado por lo que aumento en 1 el numero de habitantes esperando en ese centro

    while (habitante->datos->vacunaDisponibles[centro] == 0) { 
        pthread_cond_wait(&habitante->datos->hayVacunas[centro], &habitante->datos->mutex); 
        //con esto el hilo se duerme hasta recibir una señal de que haya una vacuna y proceder a la vacunacion recuperando el mutex
    }

    //como hemos salido del while significa que  hay al menos una vacuna disponible y por tanto el paciente ha sido vacunado
    habitante->datos->vacunaDisponibles[centro]--;
    habitante->datos->personasEnEspera[centro]--;
    
    habitante->datos->habitantesVacunados[centro]++; 

    // para que las fábricas puedan saber cuándo termina el proceso (y no quedarse esperando si ya no hay demanda)
    habitante->datos->totalVacunados++;

    printf("Habitante %d vacunado en el centro %d\n", habitante->idHiloHabitante, centro + 1); //notificamos que hilo concreto ha sido vacunado y en que centro
    fprintf(habitante->datos->fSalida, "Habitante %d vacunado en el centro %d\n", habitante->idHiloHabitante, centro + 1);

    pthread_mutex_unlock(&habitante->datos->mutex); //soltamos al mutex para que otro habitante pueda acceder a la zona critica 

    pthread_exit(NULL);
}
