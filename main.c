#include <stdio.h> 
#include <stdlib.h> 
#include <pthread.h>
#include <unistd.h>
#include <time.h> 

#define CENTROS 5 
#define FABRICAS 3
#define TOTAL_TANDAS 10
//lo que comparten las fabricas y los habitantes
typedef struct { 
    int vacunaDisponibles[CENTROS]; 
    int personasEnEspera[CENTROS];    
    pthread_mutex_t mutex;     //para que no se pisen varios thread al leer/escribir stock y esperando
    pthread_cond_t hayVacunas[CENTROS]; //señal para personas que están esperando vacunas en el centro i 
    FILE *fSalida;
    //para las estadísticas 
    int vacunasFabricadas[FABRICAS];      
    int vacunasEntregadas[FABRICAS][CENTROS]; 
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

int leerFichero(char *nombreFichero, int *habitantesTotales, int *vacunasInicialesPorCentro, int *minVacTanda, int *maxVacTanda, int *minTiempoFab, int *maxTiempoFab, int *maxTiempoReparto, int *maxTiempoReaccion, int *maxTiempoDesplaz);
void mostrarConfiguracionInicial(FILE *fSalida, int habitantesTotales, int vacunasInicialesPorCentro, int minVacTanda, int maxVacTanda, int minTiempoFab, int maxTiempoFab, int maxTiempoReparto, int maxTiempoReaccion, int maxTiempoDesplaz);
void inicializarDatos(DatosGenerales *dat, FILE *fichSal, int vacunasIniCentro, int habTotales);
int crearFabricas(pthread_t thFabricas[FABRICAS], Fabrica fabricas[FABRICAS], DatosGenerales *datos, int habitantesTotales, int minVacTanda, int maxVacTanda, int minTiempoFab, int maxTiempoFab, int maxTiempoReparto);
int ejecutarTandasHabitantes(DatosGenerales *datos, int habitantesTotales, int maxTiempoReaccion, int maxTiempoDesplaz);
void mostrarEstadisticasFinales(DatosGenerales *datos);
void limpiarDatos(DatosGenerales *datos);
void calcularReparto(int personasEnEspera[CENTROS], int total, int repartoVacunas[CENTROS]);
void* hiloFabrica(void *arg);
void* hiloHabitante(void *arg);

int main(int argc, char *argv[]) {
    char *nomSalida;
    const char *nomEntrada;
    FILE *ficheroSalida;
    DatosGenerales datos;

    int habitantesTotales;
    int vacunasIniciales;
    int minVacTanda, maxVacTanda;
    int minTiempoFab, maxTiempoFab;
    int maxTiempoReparto;
    int maxTiempoReaccion;
    int maxTiempoDesplaz;

    pthread_t hilosFab[FABRICAS];
    Fabrica fab[FABRICAS];

    srand((unsigned int)time(NULL));

    nomSalida = "salida_vacunacion.txt";
    if (argc >= 3) {
        nomSalida = argv[2];
    }

    ficheroSalida = fopen(nomSalida, "w");
    if (!ficheroSalida) {
        fprintf(stderr, "Ha habido un error al abrir el fichero");
        return 1;
    }

    nomEntrada = "entrada_vacunacion.txt";
    if (argc >= 2) {
        nomEntrada = argv[1];
    }

    if (leerFichero(nomEntrada, &habitantesTotales, &vacunasIniciales, &minVacTanda, &maxVacTanda, &minTiempoFab, &maxTiempoFab, &maxTiempoReparto, &maxTiempoReaccion, &maxTiempoDesplaz) != 0) {
        fclose(ficheroSalida);
        return 1;
    }

    mostrarConfiguracionInicial(ficheroSalida, habitantesTotales, vacunasIniciales, minVacTanda, maxVacTanda, minTiempoFab, maxTiempoFab, maxTiempoReparto, maxTiempoReaccion, maxTiempoDesplaz);
    inicializarDatos(&datos, ficheroSalida, vacunasIniciales, habitantesTotales);

    if (crearFabricas(hilosFab, fab, &datos, habitantesTotales, minVacTanda, maxVacTanda, minTiempoFab, maxTiempoFab, maxTiempoReparto) != 0) {
        limpiarDatos(&datos);
        return 1;
    }

    if (ejecutarTandasHabitantes(&datos, habitantesTotales, maxTiempoReaccion, maxTiempoDesplaz) != 0) {
        limpiarDatos(&datos);
        return 1;
    }

    for (int i = 0; i < FABRICAS; i++) {
        pthread_join(hilosFab[i], NULL);
    }

    mostrarEstadisticasFinales(&datos);
    limpiarDatos(&datos);

    return 0;
}

int leerFichero(char *nombreFichero, int *habitantesTotales, int *vacunasInicialesPorCentro, int *minVacTanda, int *maxVacTanda, int *minTiempoFab, int *maxTiempoFab, int *maxTiempoReparto, int *maxTiempoReaccion, int *maxTiempoDesplaz){
    FILE *f;

    f = fopen(nombreFichero, "r");
    if (!f) {
        perror("Error abriendo fichero de entrada");
        return 1;
    }

    if (fscanf(f, "%d", habitantesTotales) != 1 ||
        fscanf(f, "%d", vacunasInicialesPorCentro) != 1 ||
        fscanf(f, "%d", minVacTanda) != 1 ||
        fscanf(f, "%d", maxVacTanda) != 1 ||
        fscanf(f, "%d", minTiempoFab) != 1 ||
        fscanf(f, "%d", maxTiempoFab) != 1 ||
        fscanf(f, "%d", maxTiempoReparto) != 1 ||
        fscanf(f, "%d", maxTiempoReaccion) != 1 ||
        fscanf(f, "%d", maxTiempoDesplaz) != 1) {
        fprintf(stderr, "Error: el fichero no tiene los 9 números requeridos.\n");
        fclose(f);
        return 1;
    }

    fclose(f);
    return 0;
}
void mostrarConfiguracionInicial(FILE *fSalida, int habitantesTotales, int vacunasInicialesPorCentro, int minVacTanda, int maxVacTanda, int minTiempoFab, int maxTiempoFab, int maxTiempoReparto, int maxTiempoReaccion, int maxTiempoDesplaz){
    printf("VACUNACIÓN EN PANDEMIA: CONFIGURACIÓN INICIAL\n");
    fprintf(fSalida, "VACUNACIÓN EN PANDEMIA: CONFIGURACIÓN INICIAL\n");

    printf("Habitantes: %d\n", habitantesTotales);
    fprintf(fSalida, "Habitantes: %d\n", habitantesTotales);

    printf("Centros de vacunación: %d\n", CENTROS);
    fprintf(fSalida, "Centros de vacunación: %d\n", CENTROS);

    printf("Fábricas: %d\n", FABRICAS);
    fprintf(fSalida, "Fábricas: %d\n", FABRICAS);

    printf("Vacunados por tanda: %d\n", habitantesTotales / TOTAL_TANDAS);
    fprintf(fSalida, "Vacunados por tanda: %d\n", habitantesTotales / TOTAL_TANDAS);

    printf("Vacunas iniciales en cada centro: %d\n", vacunasInicialesPorCentro);
    fprintf(fSalida, "Vacunas iniciales en cada centro: %d\n", vacunasInicialesPorCentro);

    printf("Vacunas totales por fábrica: %d\n", habitantesTotales / FABRICAS);
    fprintf(fSalida, "Vacunas totales por fábrica: %d\n", habitantesTotales / FABRICAS);

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
}
void inicializarDatos(DatosGenerales *dat, FILE *fichSal, int vacunasIniCentro, int habTotales){
    int i, c;

    dat->fSalida = fichSal;
    dat->totalVacunados = 0;
    dat->habitantesTotales = habTotales;

    pthread_mutex_init(&dat->mutex, NULL);

    for (i = 0; i < CENTROS; i++) {
        dat->vacunaDisponibles[i] = vacunasIniCentro;
        dat->personasEnEspera[i] = 0;
        dat->vacunasRecibidas[i] = vacunasIniCentro;
        dat->habitantesVacunados[i] = 0;
        pthread_cond_init(&dat->hayVacunas[i], NULL);
    }

    for (i = 0; i < FABRICAS; i++) {
        dat->vacunasFabricadas[i] = 0;
        for (c = 0; c < CENTROS; c++) {
            dat->vacunasEntregadas[i][c] = 0;
        }
    }
}
int crearFabricas(pthread_t thFabricas[FABRICAS], Fabrica fabricas[FABRICAS], DatosGenerales *dat, int habitantesTotales, int minVacTanda, int maxVacTanda, int minTiempoFab, int maxTiempoFab, int maxTiempoReparto){
    int i;
    int baseCuota;
    int resto;

    baseCuota = habitantesTotales / FABRICAS;
    resto = habitantesTotales % FABRICAS;

    for (i = 0; i < FABRICAS; i++) {
        fabricas[i].idFabrica = i + 1;
        fabricas[i].vacunasTotales = baseCuota + (i < resto ? 1 : 0);
        fabricas[i].minTanda = minVacTanda;
        fabricas[i].maxTanda = maxVacTanda;
        fabricas[i].minTiempoFab = minTiempoFab;
        fabricas[i].maxTiempoFab = maxTiempoFab;
        fabricas[i].maxTiempoReparto = maxTiempoReparto;
        fabricas[i].datos = dat;

        if (pthread_create(&thFabricas[i], NULL, hiloFabrica, &fabricas[i]) != 0) {
            perror("Error creando hilo de fábrica");
            return 1;
        }
    }

    return 0;
}
int ejecutarTandasHabitantes(DatosGenerales *datos, int habitantesTotales, int maxTiempoReaccion, int maxTiempoDesplaz){
    int porTandaBase;
    int restoTandas;
    int idHabitante;
    int t, i, j;

    porTandaBase = habitantesTotales / TOTAL_TANDAS;
    restoTandas = habitantesTotales % TOTAL_TANDAS;
    idHabitante = 1;

    for (t = 0; t < TOTAL_TANDAS; t++) {
        int tamTanda = porTandaBase + (t < restoTandas ? 1 : 0);

        pthread_t *thHab = malloc(sizeof(pthread_t) * (size_t)tamTanda);
        Habitante *hab = malloc(sizeof(Habitante) * (size_t)tamTanda);

        if (!thHab || !hab) {
            fprintf(stderr, "Error: no hay memoria para crear la tanda.\n");
            free(thHab);
            free(hab);
            return 1;
        }

        for (i = 0; i < tamTanda; i++) {
            hab[i].idHiloHabitante = idHabitante++;
            hab[i].maxTiempoReaccion = maxTiempoReaccion;
            hab[i].maxTiempoDesplazamiento = maxTiempoDesplaz;
            hab[i].datos = datos;

            if (pthread_create(&thHab[i], NULL, hiloHabitante, &hab[i]) != 0) {
                perror("Error creando hilo de habitante");
                for (j = 0; j < i; j++) pthread_join(thHab[j], NULL);
                free(thHab);
                free(hab);
                return 1;
            }
        }

        for (i = 0; i < tamTanda; i++) {
            pthread_join(thHab[i], NULL);
        }

        free(thHab);
        free(hab);
    }

    return 0;
}
void mostrarEstadisticasFinales(DatosGenerales *datos) {
    int i, c;

    printf("\n--- ESTADÍSTICA FINAL ---\n");
    fprintf(datos->fSalida, "\n--- ESTADÍSTICA FINAL ---\n");

    for (i = 0; i < FABRICAS; i++) {
        printf("Fábrica %d ha fabricado %d vacunas\n", i + 1, datos->vacunasFabricadas[i]);
        fprintf(datos->fSalida, "Fábrica %d ha fabricado %d vacunas\n", i + 1, datos->vacunasFabricadas[i]);

        for (c = 0; c < CENTROS; c++) {
            printf("  Entregadas al centro %d: %d\n", c + 1, datos->vacunasEntregadas[i][c]);
            fprintf(datos->fSalida, "  Entregadas al centro %d: %d\n", c + 1, datos->vacunasEntregadas[i][c]);
        }
    }

    for (c = 0; c < CENTROS; c++) {
        int sobrantes = datos->vacunaDisponibles[c];

        printf("Centro %d: recibidas=%d, vacunados=%d, sobrantes=%d\n",
               c + 1,
               datos->vacunasRecibidas[c],
               datos->habitantesVacunados[c],
               sobrantes);

        fprintf(datos->fSalida, "Centro %d: recibidas=%d, vacunados=%d, sobrantes=%d\n",
                c + 1,
                datos->vacunasRecibidas[c],
                datos->habitantesVacunados[c],
                sobrantes);
    }

    printf("Vacunación finalizada\n");
    fprintf(datos->fSalida, "Vacunación finalizada\n");
}
void limpiarDatos(DatosGenerales *datos) {
    int i;
    for (i = 0; i < CENTROS; i++) {
        pthread_cond_destroy(&datos->hayVacunas[i]);
    }
    pthread_mutex_destroy(&datos->mutex);
    fclose(datos->fSalida);
}
void calcularReparto(int personasEnEspera[CENTROS], int total, int repartoVacunas[CENTROS]) { //esta funcion calcula cuantas vacunas de una tanda recibe cada uno de los centros en funcion de la demanda
    int i, j; 
    int v; 
    int totalPersonasEsperando= 0; 
    int vacunasAsignadas = 0; 
    int centroMayorDemanda;
    int vacunasSobrantes; 
    int copiaPersonasEnEspera[CENTROS];
    long long num;

    // Calcular el total de personas esperando y guardarlo en total
    for (i = 0; i < CENTROS; i++) {
        repartoVacunas[i] = 0; 
        copiaPersonasEnEspera[i] = personasEnEspera[i]; 
        totalPersonasEsperando += copiaPersonasEnEspera[i]; 
    }

    if (totalPersonasEsperando == 0) {
        return; 
    }

    //usamos long por si acaso hay que calcular numeros grandes, evitamos desbordamientos
    for (i = 0; i < CENTROS; i++) {
        num = (long long)total * (long long)copiaPersonasEnEspera[i]; //calcula cuantas vacunas le corresponden a ese centro en funcion de su demanda
        v = (int)(num / (long long)totalPersonasEsperando); 
        repartoVacunas[i] = v; //asigna vacunas al centro i
        vacunasAsignadas += v; //vacunas en total asignadas 
    }

    vacunasSobrantes = total - vacunasAsignadas; 
    while (vacunasSobrantes > 0) { 
        centroMayorDemanda= 0;
        for (j = 1; j < CENTROS; j++) { //busca el centro que mas gente tiene esperando
            if (copiaPersonasEnEspera[j] > copiaPersonasEnEspera[centroMayorDemanda]) {
                centroMayorDemanda = j;
            }
        }
        repartoVacunas[centroMayorDemanda]++; 
        vacunasSobrantes--;
        if (copiaPersonasEnEspera[centroMayorDemanda] > 0){ 
            copiaPersonasEnEspera[centroMayorDemanda]--; 
        }
    }
}
void* hiloFabrica(void *arg) {
    Fabrica *f = (Fabrica*) arg; 
    int fabricadas = 0; //va contando el numero de vacunas que lleva hechas la fabrica 
    int tiempoFab;
    int tanda;
    int tiempoReparto;
    int demanda[CENTROS];
    int reparto[CENTROS];
    int sumaDemanda = 0;


    while (fabricadas < f->vacunasTotales) { //la fabrica trabaja hasta que fabrica todas las vacunas que le corresponden 
        //Fabrica una tanda
        tanda = rand() % (f->maxTanda - f->minTanda + 1) + f->minTanda; 
        if (fabricadas + tanda > f->vacunasTotales){ 
            tanda = f->vacunasTotales - fabricadas; 
        }
        tiempoFab = rand() % (f->maxTiempoFab - f->minTiempoFab + 1) + f->minTiempoFab; 
        printf("Fábrica %d prepara %d vacunas\n", f->idFabrica, tanda);
        fprintf(f->datos->fSalida, "Fábrica %d prepara %d vacunas\n", f->idFabrica, tanda);

        sleep((unsigned int)tiempoFab); //el hilo lo dormimos durante el tiempo de fabricacion
        fabricadas += tanda; 
        //Vamos a modificar un dato compartido entre threads. (Para la estadística) 
        pthread_mutex_lock(&f->datos->mutex);
        f->datos->vacunasFabricadas[f->idFabrica - 1] += tanda; 
        pthread_mutex_unlock(&f->datos->mutex);

        //Decidir cómo repartir la tanda según la demanda
        pthread_mutex_lock(&f->datos->mutex); 
        sumaDemanda = 0;
        for (int i = 0; i < CENTROS; i++) {
            demanda[i] = f->datos->personasEnEspera[i];
            if (demanda[i] > 0) {
                sumaDemanda += demanda[i];
            }
        }
        pthread_mutex_unlock(&f->datos->mutex);

        // Si hay demanda (sumaDemanda), reparto proporcional. Si no, hacemos un reparto equilibrado.
        if (sumaDemanda > 0) {
            calcularReparto(demanda, tanda, reparto);
        } else {
            for (int i = 0; i < CENTROS; i++) {
                reparto[i] = 0;
            }
            for (int k = 0; k < tanda; k++) {
                reparto[k % CENTROS]++; 
            }
        }
        //Repartir vacunas a cada centro
        for (int i = 0; i < CENTROS; i++) {
            if (reparto[i] <= 0){
                continue;
            } 
            // Simula el tiempo de reparto a este centro
            tiempoReparto = rand() % f->maxTiempoReparto + 1;
            sleep((unsigned int)tiempoReparto);
            // Actualizar vacunas disponibles del centro
            pthread_mutex_lock(&f->datos->mutex);
            f->datos->vacunaDisponibles[i] += reparto[i];
            // Estadísticas: entregadas por fábrica y recibidas por centro
            f->datos->vacunasEntregadas[f->idFabrica - 1][i] += reparto[i];
            f->datos->vacunasRecibidas[i] += reparto[i];
            // Si han llegado vacunas, despertamos a los que estén esperando en ese centro
            for (int v = 0; v < reparto[i]; v++) {
                pthread_cond_signal(&f->datos->hayVacunas[i]);
            }
            pthread_mutex_unlock(&f->datos->mutex);
            
            printf("Fábrica %d entrega %d vacunas en el centro %d\n", f->idFabrica, reparto[i], i + 1);
            fprintf(f->datos->fSalida, "Fábrica %d entrega %d vacunas en el centro %d\n", f->idFabrica, reparto[i], i + 1);
        }
    }

    printf("Fábrica %d ha fabricado todas sus vacunas\n", f->idFabrica);
    fprintf(f->datos->fSalida, "Fábrica %d ha fabricado todas sus vacunas\n", f->idFabrica);

    pthread_exit(NULL);
}
void* hiloHabitante(void *arg) { //cada habitante es un hilo que posee esta funcion
    Habitante *habitante = (Habitante*) arg; //es un puntero a la estructura habitante lo que permite acceder y modificar sus datos

    sleep((rand() % habitante->maxTiempoReaccion + 1)); 

    int centro = rand() % CENTROS; //Seleccion de centro aleatorio
    printf("Habitante %d elige el centro %d para vacunarse\n", habitante->idHiloHabitante, centro + 1);
    fprintf(habitante->datos->fSalida, "Habitante %d elige el centro %d para vacunarse\n", habitante->idHiloHabitante, centro + 1);
   
    sleep((rand() % habitante->maxTiempoDesplazamiento + 1)); 

    //os personas no pueden vacunarse a la vez en el mismo centro
    pthread_mutex_lock(&habitante->datos->mutex); 
    habitante->datos->personasEnEspera[centro]++; //El habitante esta disponible para ser vacunado por lo que aumentamos el numero de habitantes esperando en ese centro

    while (habitante->datos->vacunaDisponibles[centro] == 0) { 
        pthread_cond_wait(&habitante->datos->hayVacunas[centro], &habitante->datos->mutex); 
        //con esto el hilo se duerme hasta recibir una señal de que hay una vacuna y proceder a la vacunacion recuperando el mutex
    }

    //como hemos salido del while significa que  hay al menos una vacuna disponible y por tanto el paciente ha sido vacunado
    habitante->datos->vacunaDisponibles[centro]--;
    habitante->datos->personasEnEspera[centro]--;
    habitante->datos->habitantesVacunados[centro]++; 
    // para que las fábricas puedan saber cuándo termina el proceso 
    habitante->datos->totalVacunados++;
    printf("Habitante %d vacunado en el centro %d\n", habitante->idHiloHabitante, centro + 1); //notificamos que habitante ha sido vacunado y en que centro
    fprintf(habitante->datos->fSalida, "Habitante %d vacunado en el centro %d\n", habitante->idHiloHabitante, centro + 1);

    pthread_mutex_unlock(&habitante->datos->mutex); 

    pthread_exit(NULL);
}
