#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>

#define CENTROS 5

typedef struct {
    int stock[CENTROS];        // Vacunas disponibles por centro
    int esperando[CENTROS];    // Personas esperando en cada centro
    pthread_mutex_t mutex;     
    pthread_cond_t hayVacunas[CENTROS];
} DatosCompartidos;

typedef struct {
    int id;
    int vacunasTotales;
    int minTanda, maxTanda;
    int minTiempoFab, maxTiempoFab;
    int maxTiempoReparto;
    DatosCompartidos *datos;
} Fabrica;

void* hiloFabrica(void *arg) {
    Fabrica *f = (Fabrica*) arg;
    int fabricadas = 0;

    while (fabricadas < f->vacunasTotales) {

        // 1️⃣ Fabricar una tanda
        int tanda = rand() % (f->maxTanda - f->minTanda + 1) + f->minTanda;
        if (fabricadas + tanda > f->vacunasTotales)
            tanda = f->vacunasTotales - fabricadas;

        int tiempo = rand() % (f->maxTiempoFab - f->minTiempoFab + 1) + f->minTiempoFab;

        printf("Fábrica %d prepara %d vacunas\n", f->id, tanda);
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
                f->datos->stock[i]++;
                tanda--;
                printf("Fábrica %d entrega 1 vacuna al centro %d\n", f->id, i + 1);
                pthread_cond_signal(&f->datos->hayVacunas[i]);
            }
        }

        // 🔹 Reparto del resto de forma equilibrada
        int i = 0;
        while (tanda > 0) {
            f->datos->stock[i % CENTROS]++;
            printf("Fábrica %d entrega 1 vacuna al centro %d\n", f->id, (i % CENTROS) + 1);
            pthread_cond_signal(&f->datos->hayVacunas[i % CENTROS]);
            tanda--;
            i++;
        }

        pthread_mutex_unlock(&f->datos->mutex);

        // Simula el tiempo de reparto
        sleep(rand() % f->maxTiempoReparto + 1);
    }

    printf("Fábrica %d ha fabricado todas sus vacunas\n", f->id);
    pthread_exit(NULL);
}

