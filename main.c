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
void mostrarConfiguracionInicial(FILE *fichSalida, int habTotales, int vIniCentro, int minVacTanda, int maxVacTanda, int minTiempoFab, int maxTiempoFab, int maxTiempoReparto, int maxTiempoReaccion, int maxTiempoDesplaz);
void inicializarDatos(DatosGenerales *dat, FILE *fichSal, int vacunasIniCentro, int habTotales);
int crearFabricas(pthread_t thFab[FABRICAS], Fabrica fabr[FABRICAS], DatosGenerales *dat, int habitTotal, int minVacTanda, int maxVacTanda, int minTiempoFab, int maxTiempoFab, int maxTiempoReparto);
int ejecutarTandasHabitantes(DatosGenerales *datos, int habitantesTotales, int maxTiempoReaccion, int maxTiempoDesplaz);
void mostrarEstadisticasFinales(DatosGenerales *datos);
void destruirDatos(DatosGenerales *dat);
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

    if (argc >= 3) {
        nomSalida = argv[2];
    }else{
        nomSalida = "salida_vacunacion.txt";
    }
    ficheroSalida = fopen(nomSalida, "w");
    if (!ficheroSalida) {
        fprintf(stderr, "Ha habido un error al abrir el fichero");
        return 1;
    }

    if (argc >= 2) {
        nomEntrada = argv[1];
    }else{
        nomEntrada = "entrada_vacunacion.txt";
    }

    if (leerFichero(nomEntrada, &habitantesTotales, &vacunasIniciales, &minVacTanda, &maxVacTanda, &minTiempoFab, &maxTiempoFab, &maxTiempoReparto, &maxTiempoReaccion, &maxTiempoDesplaz) != 0) {
        fclose(ficheroSalida);
        return 1;
    }

    mostrarConfiguracionInicial(ficheroSalida, habitantesTotales, vacunasIniciales, minVacTanda, maxVacTanda, minTiempoFab, maxTiempoFab, maxTiempoReparto, maxTiempoReaccion, maxTiempoDesplaz);
    inicializarDatos(&datos, ficheroSalida, vacunasIniciales, habitantesTotales);

    if (crearFabricas(hilosFab, fab, &datos, habitantesTotales, minVacTanda, maxVacTanda, minTiempoFab, maxTiempoFab, maxTiempoReparto) != 0) {
        destruirDatos(&datos);
        return 1;
    }

    if (ejecutarTandasHabitantes(&datos, habitantesTotales, maxTiempoReaccion, maxTiempoDesplaz) != 0) {
        destruirDatos(&datos);
        return 1;
    }

    for (int i = 0; i < FABRICAS; i++) {
        pthread_join(hilosFab[i], NULL);
    }

    mostrarEstadisticasFinales(&datos);
    destruirDatos(&datos);

    return 0;
}

int leerFichero(char *nomFichero, int *habTotal, int *vacIniCentro, int *minVacTanda, int *maxVacTanda, int *minTiempoFab, int *maxTiempoFab, int *maxTiempoReparto, int *maxTiempoReaccion, int *maxTiempoDesplaz){
    FILE *f;

    f = fopen(nomFichero, "r");
    if (!f) {
        perror("Error abriendo fichero de entrada");
        return 1;
    }
    if (fscanf(f, "%d", habTotal) != 1 ||
        fscanf(f, "%d", vacIniCentro) != 1 ||
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
void mostrarConfiguracionInicial(FILE *fichSalida, int habTotales, int vIniCentro, int minVacTanda, int maxVacTanda, int minTiempoFab, int maxTiempoFab, int maxTiempoReparto, int maxTiempoReaccion, int maxTiempoDesplaz){
    printf("VACUNACIÓN EN PANDEMIA: CONFIGURACIÓN INICIAL\n");
    fprintf(fichSalida, "VACUNACIÓN EN PANDEMIA: CONFIGURACIÓN INICIAL\n");

    printf("Habitantes: %d\n", habTotales);
    fprintf(fichSalida, "Habitantes: %d\n", habTotales);

    printf("Centros de vacunación: %d\n", CENTROS);
    fprintf(fichSalida, "Centros de vacunación: %d\n", CENTROS);

    printf("Fábricas: %d\n", FABRICAS);
    fprintf(fichSalida, "Fábricas: %d\n", FABRICAS);

    printf("Vacunados por tanda: %d\n", habTotales / TOTAL_TANDAS);
    fprintf(fichSalida, "Vacunados por tanda: %d\n", habTotales / TOTAL_TANDAS);

    printf("Vacunas iniciales en cada centro: %d\n", vIniCentro);
    fprintf(fichSalida, "Vacunas iniciales en cada centro: %d\n", vIniCentro);

    printf("Vacunas totales por fábrica: %d\n", habTotales / FABRICAS);
    fprintf(fichSalida, "Vacunas totales por fábrica: %d\n", habTotales / FABRICAS);

    printf("Mínimo número de vacunas fabricadas en cada tanda: %d\n", minVacTanda);
    fprintf(fichSalida, "Mínimo número de vacunas fabricadas en cada tanda: %d\n", minVacTanda);

    printf("Máximo número de vacunas fabricadas en cada tanda: %d\n", maxVacTanda);
    fprintf(fichSalida, "Máximo número de vacunas fabricadas en cada tanda: %d\n", maxVacTanda);

    printf("Tiempo mínimo de fabricación de una tanda de vacunas: %d\n", minTiempoFab);
    fprintf(fichSalida, "Tiempo mínimo de fabricación de una tanda de vacunas: %d\n", minTiempoFab);

    printf("Tiempo máximo de fabricación de una tanda de vacunas: %d\n", maxTiempoFab);
    fprintf(fichSalida, "Tiempo máximo de fabricación de una tanda de vacunas: %d\n", maxTiempoFab);

    printf("Tiempo máximo de reparto de vacunas a los centros: %d\n", maxTiempoReparto);
    fprintf(fichSalida, "Tiempo máximo de reparto de vacunas a los centros: %d\n", maxTiempoReparto);

    printf("Tiempo máximo que un habitante tarda en ver que está citado para vacunarse: %d\n", maxTiempoReaccion);
    fprintf(fichSalida, "Tiempo máximo que un habitante tarda en ver que está citado para vacunarse: %d\n", maxTiempoReaccion);

    printf("Tiempo máximo de desplazamiento del habitante al centro de vacunación: %d\n", maxTiempoDesplaz);
    fprintf(fichSalida, "Tiempo máximo de desplazamiento del habitante al centro de vacunación: %d\n", maxTiempoDesplaz);

    printf("PROCESO DE VACUNACIÓN\n");
    fprintf(fichSalida, "PROCESO DE VACUNACIÓN\n");
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
int crearFabricas(pthread_t thFab[FABRICAS], Fabrica fabr[FABRICAS], DatosGenerales *dat, int habitTotal, int minVacTanda, int maxVacTanda, int minTiempoFab, int maxTiempoFab, int maxTiempoReparto){
    int i;
    int vacunasPorFab;
    int resto;
    vacunasPorFab= habitTotal / FABRICAS;
    for (i = 0; i < FABRICAS; i++) {
        fabr[i].idFabrica = i + 1;
        fabr[i].vacunasTotales = vacunasPorFab;
        fabr[i].minTanda = minVacTanda;
        fabr[i].maxTanda = maxVacTanda;
        fabr[i].minTiempoFab = minTiempoFab;
        fabr[i].maxTiempoFab = maxTiempoFab;
        fabr[i].maxTiempoReparto = maxTiempoReparto;
        fabr[i].datos = dat;

        if (pthread_create(&thFab[i], NULL, hiloFabrica, &fabr[i]) != 0) {
            fprintf(stderr, "Hubo un error al crear el hilo de la fabrica");
            return 1;
        }
    }
    return 0;
}
int ejecutarTandasHabitantes(DatosGenerales *datos, int habitantesTotales, int maxTiempoReaccion, int maxTiempoDesplaz){
    int habitantesTanda;
    int idHabitante;
    int t, i, j;

    habitantesTanda = habitantesTotales / TOTAL_TANDAS;
    idHabitante = 1;

    for (t = 0; t < TOTAL_TANDAS; t++) { //cada tanda crea 120 hilos

        pthread_t *hiloHab = malloc(sizeof(pthread_t) * (size_t)habitantesTanda);
        Habitante *hab = malloc(sizeof(Habitante) * (size_t)habitantesTanda);

        if (!hiloHab || !hab) {
            fprintf(stderr, "Error: no hay memoria para crear la tanda.\n");
            free(hiloHab);
            free(hab);
            return 1;
        }

        for (i = 0; i < habitantesTanda; i++) { //inicializamos structura hab
            hab[i].idHiloHabitante = idHabitante++;
            hab[i].maxTiempoReaccion = maxTiempoReaccion;
            hab[i].maxTiempoDesplazamiento = maxTiempoDesplaz;
            hab[i].datos = datos;

            pthread_create(&hiloHab[i], NULL, hiloHabitante, &hab[i]);
               
            for (j = 0; j < i; j++)  {
                pthread_join(hiloHab[j], NULL);
                free(hiloHab);
                free(hab);
                return 1;
            }
        }

        for (i = 0; i < habitantesTanda; i++) {
            pthread_join(hiloHab[i], NULL);
        }

        free(hiloHab);
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

        printf("Centro %d: recibidas=%d, vacunados=%d, sobrantes=%d\n", c + 1, datos->vacunasRecibidas[c], datos->habitantesVacunados[c], sobrantes);

        fprintf(datos->fSalida, "Centro %d: recibidas=%d, vacunados=%d, sobrantes=%d\n", c + 1, datos->vacunasRecibidas[c], datos->habitantesVacunados[c], sobrantes);
    }

    printf("Vacunación finalizada\n");
    fprintf(datos->fSalida, "Vacunación finalizada\n");
}

void destruirDatos(DatosGenerales *dat) {
    int i;
    for (i = 0; i < CENTROS; i++) {
        pthread_cond_destroy(&dat->hayVacunas[i]);
    }
    pthread_mutex_destroy(&dat->mutex);
    fclose(dat->fSalida);
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
