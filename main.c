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
    int minTanda, maxTanda; //rango de vacunas que produce en cada tanda
    int minTiempoFab, maxTiempoFab; //tiempo aleatorio de fabricacion por tanda
    int maxTiempoReparto; //tiempo aleatorio del reparto
    DatosCompartidos *datos; //puntero para poder tocar stock[], esperando []
} Fabrica;

typedef struct {
    int id;
    int maxTiempoReaccion;
    int maxTiempoDesplazamiento;
    DatosCompartidos *datos;
} Habitante;

void* hiloFabrica(void *arg) {
    Fabrica *f = (Fabrica*) arg;
    int fabricadas = 0;

    while (fabricadas < f->vacunasTotales) {

        // 1️⃣ Fabricar una tanda
        int tanda = rand() % (f->maxTanda - f->minTanda + 1) + f->minTanda;
        if (fabricadas + tanda > f->vacunasTotales)
            tanda = f->vacunasTotales - fabricadas;

        int tiempo = rand() % (f->maxTiempoFab - f->minTiempoFab + 1) + f->minTiempoFab;

        printf("Fábrica %d prepara %d vacunas\n", f->idFabrica, tanda);
        sleep(tiempo);

        fabricadas += tanda;

        // 2️⃣ Reparto
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

void* hiloHabitante(void *arg) {
    Habitante *h = (Habitante*) arg;

    /* 1️⃣ Tiempo hasta darse cuenta de la cita */
    sleep(rand() % h->maxTiempoReaccion + 1);

    /* 2️⃣ Elegir centro */
    int centro = rand() % CENTROS;
    printf("Habitante %d elige el centro %d para vacunarse\n",
           h->id, centro + 1);

    /* 3️⃣ Desplazamiento al centro */
    sleep(rand() % h->maxTiempoDesplazamiento + 1);

    /* 4️⃣ Intento de vacunación */
    pthread_mutex_lock(&h->datos->mutex);

    while (h->datos->vacunaDisponibles[centro] == 0) {
        /* No hay vacunas → esperar */
        h->datos->esperando[centro]++;
        pthread_cond_wait(&h->datos->hayVacunas[centro],
                          &h->datos->mutex);
        h->datos->esperando[centro]--;
    }

    /* Vacunación */
    h->datos->vacunaDisponibles[centro]--;
    printf("Habitante %d vacunado en el centro %d\n",
           h->id, centro + 1);

    pthread_mutex_unlock(&h->datos->mutex);

    pthread_exit(NULL);
}

