/* vacunacion.c
 * Práctica 2 - Programación con threads (Sistemas Operativos)
 *
 * Requisitos implementados:
 *  - 5 centros, 3 fábricas.
 *  - Habitantes vacunados por tandas (10 tandas). Se crean threads de habitantes por tanda.
 *  - Fábricas fabrican por tandas aleatorias y reparten según demanda (evita inanición).
 *  - Sin interbloqueos: un único mutex para el estado compartido.
 *  - Impresión por pantalla y a fichero, de forma thread-safe.
 *  - Estadísticas finales por fábrica y por centro.
 *
 * Nota: Se usan sleeps en segundos para emular tiempos (rand()).
 */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>

#define CENTROS 5
#define FABRICAS 3
#define TANDAS 10

typedef struct {
    int habitantes_totales;
    int vacunas_iniciales_por_centro;
    int min_vac_tanda_fab;
    int max_vac_tanda_fab;
    int min_t_fabricacion;
    int max_t_fabricacion;
    int max_t_reparto;      /* min = 1 */
    int max_t_reaccion;     /* min = 1 */
    int max_t_desplaz;      /* min = 1 */
} Config;

typedef struct {
    /* Estado de centros */
    int stock[CENTROS];                 /* vacunas disponibles por centro */
    int esperando[CENTROS];             /* habitantes esperando en centro */
    int demand_total[CENTROS];          /* habitantes que han elegido ese centro (total) */
    int vacunados_centro[CENTROS];      /* habitantes vacunados en ese centro */
    int recibidas_centro[CENTROS];      /* vacunas recibidas en total por centro */

    /* Totales */
    int vacunados_totales;

    /* Estadísticas por fábrica */
    int fabricadas_por_fabrica[FABRICAS];
    int entregadas_por_fabrica[FABRICAS][CENTROS];

    /* Sincronización */
    pthread_mutex_t mtx;                /* protege TODO el estado compartido */
    pthread_cond_t hay_vacunas[CENTROS];

    /* Logging */
    pthread_mutex_t log_mtx;
    FILE *fout;
} Shared;

typedef struct {
    int id_habitante;       /* 1..N */
    Config *cfg;
    Shared *S;
    unsigned int seed;
} HabitanteArgs;

typedef struct {
    int id_fabrica;         /* 1..3 */
    int cuota_total;        /* vacunas que debe fabricar esa fábrica */
    Config *cfg;
    Shared *S;
    unsigned int seed;
} FabricaArgs;

/* -------- utilidades -------- */

static int rand_between(unsigned int *seed, int a, int b) {
    int r;
    if (b <= a) return a;
    r = (int)(rand_r(seed) % (unsigned int)(b - a + 1));
    return a + r;
}

static void log_print(Shared *S, const char *fmt, ...) {
    va_list ap;
    va_list ap2;

    pthread_mutex_lock(&S->log_mtx);

    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);

    if (S->fout != NULL) {
        va_start(ap2, fmt);
        vfprintf(S->fout, fmt, ap2);
        va_end(ap2);
        fflush(S->fout);
    }

    fflush(stdout);

    pthread_mutex_unlock(&S->log_mtx);
}

static int leer_config(const char *path, Config *cfg) {
    FILE *f;
    int vals[9];
    int i;

    f = fopen(path, "r");
    if (!f) return -1;

    for (i = 0; i < 9; i++) {
        if (fscanf(f, "%d", &vals[i]) != 1) {
            fclose(f);
            return -2;
        }
    }
    fclose(f);

    cfg->habitantes_totales = vals[0];
    cfg->vacunas_iniciales_por_centro = vals[1];
    cfg->min_vac_tanda_fab = vals[2];
    cfg->max_vac_tanda_fab = vals[3];
    cfg->min_t_fabricacion = vals[4];
    cfg->max_t_fabricacion = vals[5];
    cfg->max_t_reparto = vals[6];
    cfg->max_t_reaccion = vals[7];
    cfg->max_t_desplaz = vals[8];

    /* Validaciones básicas */
    if (cfg->habitantes_totales <= 0) return -3;
    if (cfg->vacunas_iniciales_por_centro < 0) return -3;
    if (cfg->min_vac_tanda_fab <= 0 || cfg->max_vac_tanda_fab < cfg->min_vac_tanda_fab) return -3;
    if (cfg->min_t_fabricacion < 0 || cfg->max_t_fabricacion < cfg->min_t_fabricacion) return -3;
    if (cfg->max_t_reparto < 1) return -3;
    if (cfg->max_t_reaccion < 1) return -3;
    if (cfg->max_t_desplaz < 1) return -3;

    return 0;
}

/* -------- reparto por demanda (anti-inanición) --------
   Idea:
   - "Necesidad" aproximada de un centro = (demand_total - vacunados_centro) - stock
   - Peso = 2*esperando + necesidad (si es positiva)
   - Antiinanición: si hay suficientes vacunas, se da al menos 1 a cada centro con esperando>0
*/
static void repartir_tanda(Shared *S, int id_fabrica_0based, int vacunas_tanda, unsigned int *seed, int max_t_reparto) {
    int give[CENTROS];
    int weight[CENTROS];
    int totalW;
    int restantes;
    int i;
    int c;
    int centros_con_espera;
    int entrega_tiempo;

    for (i = 0; i < CENTROS; i++) give[i] = 0;

    /* Simula tiempo previo al reparto de la tanda */
    entrega_tiempo = rand_between(seed, 1, max_t_reparto);
    sleep((unsigned int)entrega_tiempo);

    pthread_mutex_lock(&S->mtx);

    totalW = 0;
    centros_con_espera = 0;
    for (c = 0; c < CENTROS; c++) {
        int remaining_people;
        int necesidad;

        remaining_people = S->demand_total[c] - S->vacunados_centro[c];
        if (remaining_people < 0) remaining_people = 0;

        necesidad = remaining_people - S->stock[c];
        if (necesidad < 0) necesidad = 0;

        weight[c] = 2 * S->esperando[c] + necesidad;
        if (S->esperando[c] > 0) centros_con_espera++;
        totalW += weight[c];
    }

    restantes = vacunas_tanda;

    /* 1) Antiinanición: 1 vacuna a cada centro con espera si se puede */
    if (centros_con_espera > 0 && restantes >= centros_con_espera) {
        for (c = 0; c < CENTROS; c++) {
            if (S->esperando[c] > 0) {
                give[c] += 1;
                restantes--;
            }
        }
    }

    /* 2) Reparto del resto */
    if (restantes > 0) {
        if (totalW == 0) {
            /* Nadie espera y no hay necesidad -> round-robin uniforme */
            c = 0;
            while (restantes > 0) {
                give[c]++;
                restantes--;
                c++;
                if (c == CENTROS) c = 0;
            }
        } else {
            int allocated;
            int leftovers;

            allocated = 0;
            for (c = 0; c < CENTROS; c++) {
                int part;

                if (weight[c] <= 0) continue;
                part = (restantes * weight[c]) / totalW;
                if (part < 0) part = 0;

                give[c] += part;
                allocated += part;
            }

            leftovers = restantes - allocated;

            /* Ajuste de sobrantes: al centro con mayor peso cada vez */
            while (leftovers > 0) {
                int best;

                best = 0;
                for (c = 1; c < CENTROS; c++) {
                    if (weight[c] > weight[best]) best = c;
                }
                give[best] += 1;
                leftovers--;

                if (weight[best] > 0) weight[best]--; /* leve penalización */
            }
        }
    }

    /* 3) Aplicar reparto + estadísticas + despertar */
    for (c = 0; c < CENTROS; c++) {
        if (give[c] > 0) {
            S->stock[c] += give[c];
            S->recibidas_centro[c] += give[c];
            S->entregadas_por_fabrica[id_fabrica_0based][c] += give[c];

            /* Despertamos a los que esperan en ese centro */
            pthread_cond_broadcast(&S->hay_vacunas[c]);
        }
    }

    pthread_mutex_unlock(&S->mtx);

    /* Prints fuera del mutex para no bloquear a otros (pero log_print es thread-safe) */
    for (c = 0; c < CENTROS; c++) {
        if (give[c] > 0) {
            log_print(S, "Fábrica %d entrega %d vacunas en el centro %d\n",
                      id_fabrica_0based + 1, give[c], c + 1);
        }
    }
}

/* -------- threads -------- */

static void* thread_habitante(void *arg) {
    HabitanteArgs *A;
    int reaccion;
    int centro;
    int desplaz;

    A = (HabitanteArgs*)arg;

    /* Tiempo en darse cuenta de la cita (min=1) */
    reaccion = rand_between(&A->seed, 1, A->cfg->max_t_reaccion);
    sleep((unsigned int)reaccion);

    /* Elegir centro (aleatorio) */
    centro = rand_between(&A->seed, 0, CENTROS - 1);

    pthread_mutex_lock(&A->S->mtx);
    A->S->demand_total[centro] += 1;
    A->S->esperando[centro] += 1;
    pthread_mutex_unlock(&A->S->mtx);

    log_print(A->S, "Habitante %d elige el centro %d para vacunarse\n", A->id_habitante, centro + 1);

    /* Tiempo de desplazamiento (min=1) */
    desplaz = rand_between(&A->seed, 1, A->cfg->max_t_desplaz);
    sleep((unsigned int)desplaz);

    /* Vacunarse o esperar */
    pthread_mutex_lock(&A->S->mtx);

    while (A->S->stock[centro] <= 0) {
        pthread_cond_wait(&A->S->hay_vacunas[centro], &A->S->mtx);
    }

    /* Ya hay vacuna */
    A->S->stock[centro] -= 1;
    A->S->esperando[centro] -= 1;
    A->S->vacunados_centro[centro] += 1;
    A->S->vacunados_totales += 1;

    pthread_mutex_unlock(&A->S->mtx);

    log_print(A->S, "Habitante %d vacunado en el centro %d\n", A->id_habitante, centro + 1);

    pthread_exit(NULL);
    return NULL;
}

static void* thread_fabrica(void *arg) {
    FabricaArgs *F;
    int fabricadas;

    F = (FabricaArgs*)arg;
    fabricadas = 0;

    while (fabricadas < F->cuota_total) {
        int left;
        int tanda;
        int t_fab;

        left = F->cuota_total - fabricadas;

        /* Cantidad aleatoria por tanda */
        tanda = rand_between(&F->seed, F->cfg->min_vac_tanda_fab, F->cfg->max_vac_tanda_fab);
        if (tanda > left) tanda = left;

        /* Tiempo de fabricación */
        t_fab = rand_between(&F->seed, F->cfg->min_t_fabricacion, F->cfg->max_t_fabricacion);

        log_print(F->S, "Fábrica %d prepara %d vacunas\n", F->id_fabrica, tanda);
        sleep((unsigned int)t_fab);

        /* Actualiza estadísticas de fabricación */
        pthread_mutex_lock(&F->S->mtx);
        F->S->fabricadas_por_fabrica[F->id_fabrica - 1] += tanda;
        pthread_mutex_unlock(&F->S->mtx);

        fabricadas += tanda;

        /* Reparto según demanda */
        repartir_tanda(F->S, F->id_fabrica - 1, tanda, &F->seed, F->cfg->max_t_reparto);
    }

    log_print(F->S, "Fábrica %d ha fabricado todas sus vacunas\n", F->id_fabrica);
    pthread_exit(NULL);
    return NULL;
}

/* -------- main -------- */

int main(int argc, char **argv) {
    const char *fich_entrada;
    const char *fich_salida;
    Config cfg;
    Shared S;

    pthread_t th_fabricas[FABRICAS];
    FabricaArgs fab_args[FABRICAS];

    int i;
    int c;
    int ret;

    int base_cuota;
    int resto_cuota;

    int vacunados_por_tanda_base;
    int resto_tandas;

    int next_id;

    /* Argumentos con valores por defecto */
    fich_entrada = "entrada_vacunacion.txt";
    fich_salida = "salida_vacunacion.txt";

    if (argc >= 2) fich_entrada = argv[1];
    if (argc >= 3) fich_salida = argv[2];

    ret = leer_config(fich_entrada, &cfg);
    if (ret != 0) {
        fprintf(stderr, "Error leyendo config '%s' (código %d). Revisa formato (9 enteros, 1 por línea).\n",
                fich_entrada, ret);
        return 1;
    }

    memset(&S, 0, sizeof(Shared));

    pthread_mutex_init(&S.mtx, NULL);
    pthread_mutex_init(&S.log_mtx, NULL);
    for (c = 0; c < CENTROS; c++) {
        pthread_cond_init(&S.hay_vacunas[c], NULL);
    }

    S.fout = fopen(fich_salida, "w");
    if (!S.fout) {
        fprintf(stderr, "No puedo abrir fichero de salida '%s': %s\n", fich_salida, strerror(errno));
        /* seguimos solo por pantalla */
        S.fout = NULL;
    }

    /* Inicializa stock inicial */
    for (c = 0; c < CENTROS; c++) {
        S.stock[c] = cfg.vacunas_iniciales_por_centro;
        S.recibidas_centro[c] = cfg.vacunas_iniciales_por_centro; /* cuenta como recibidas inicialmente */
    }

    /* Cálculos de cuotas: total vacunas fabricadas = habitantes */
    base_cuota = cfg.habitantes_totales / FABRICAS;
    resto_cuota = cfg.habitantes_totales % FABRICAS;

    /* 10 tandas para toda la población */
    vacunados_por_tanda_base = cfg.habitantes_totales / TANDAS;
    resto_tandas = cfg.habitantes_totales % TANDAS;

    /* Configuración inicial */
    log_print(&S, "VACUNACIÓN EN PANDEMIA: CONFIGURACIÓN INICIAL\n");
    log_print(&S, "Habitantes: %d\n", cfg.habitantes_totales);
    log_print(&S, "Centros de vacunación: %d\n", CENTROS);
    log_print(&S, "Fábricas: %d\n", FABRICAS);
    log_print(&S, "Tandas de vacunación: %d\n", TANDAS);
    log_print(&S, "Vacunas iniciales en cada centro: %d\n", cfg.vacunas_iniciales_por_centro);
    log_print(&S, "Vacunas totales por fábrica (aprox): %d (+reparto de resto)\n", base_cuota);
    log_print(&S, "Mínimo vacunas por tanda: %d\n", cfg.min_vac_tanda_fab);
    log_print(&S, "Máximo vacunas por tanda: %d\n", cfg.max_vac_tanda_fab);
    log_print(&S, "Tiempo mínimo fabricación: %d\n", cfg.min_t_fabricacion);
    log_print(&S, "Tiempo máximo fabricación: %d\n", cfg.max_t_fabricacion);
    log_print(&S, "Tiempo máximo reparto: %d\n", cfg.max_t_reparto);
    log_print(&S, "Tiempo máximo reacción habitante: %d\n", cfg.max_t_reaccion);
    log_print(&S, "Tiempo máximo desplazamiento: %d\n", cfg.max_t_desplaz);
    log_print(&S, "PROCESO DE VACUNACIÓN\n");

    /* Lanzar fábricas */
    for (i = 0; i < FABRICAS; i++) {
        int cuota;

        cuota = base_cuota;
        if (i < resto_cuota) cuota += 1;

        fab_args[i].id_fabrica = i + 1;
        fab_args[i].cuota_total = cuota;
        fab_args[i].cfg = &cfg;
        fab_args[i].S = &S;
        fab_args[i].seed = (unsigned int)time(NULL) ^ (unsigned int)(0x9e3779b9u * (i + 1));

        if (pthread_create(&th_fabricas[i], NULL, thread_fabrica, &fab_args[i]) != 0) {
            fprintf(stderr, "Error creando thread de fábrica %d\n", i + 1);
            return 2;
        }
    }

    /* Lanzar habitantes por tandas */
    next_id = 1;
    for (i = 0; i < TANDAS; i++) {
        int tam_tanda;
        pthread_t *th_habs;
        HabitanteArgs *args_habs;
        int j;

        tam_tanda = vacunados_por_tanda_base;
        if (i < resto_tandas) tam_tanda += 1;

        th_habs = (pthread_t*)malloc(sizeof(pthread_t) * (size_t)tam_tanda);
        args_habs = (HabitanteArgs*)malloc(sizeof(HabitanteArgs) * (size_t)tam_tanda);

        if (!th_habs || !args_habs) {
            fprintf(stderr, "Error de memoria creando tanda %d\n", i + 1);
            free(th_habs);
            free(args_habs);
            return 3;
        }

        for (j = 0; j < tam_tanda; j++) {
            args_habs[j].id_habitante = next_id;
            args_habs[j].cfg = &cfg;
            args_habs[j].S = &S;
            args_habs[j].seed = (unsigned int)time(NULL) ^ (unsigned int)(0x7f4a7c15u * (next_id + 17));

            if (pthread_create(&th_habs[j], NULL, thread_habitante, &args_habs[j]) != 0) {
                fprintf(stderr, "Error creando thread habitante %d\n", next_id);
                /* join de los ya creados */
                while (j > 0) {
                    j--;
                    pthread_join(th_habs[j], NULL);
                }
                free(th_habs);
                free(args_habs);
                return 4;
            }

            next_id++;
        }

        /* Esperar a que termine la tanda completa */
        for (j = 0; j < tam_tanda; j++) {
            pthread_join(th_habs[j], NULL);
        }

        free(th_habs);
        free(args_habs);
    }

    /* Esperar fábricas */
    for (i = 0; i < FABRICAS; i++) {
        pthread_join(th_fabricas[i], NULL);
    }

    log_print(&S, "Vacunación finalizada\n");

    /* Estadísticas finales */
    log_print(&S, "\nESTADÍSTICAS FINALES\n");

    for (i = 0; i < FABRICAS; i++) {
        log_print(&S, "Fábrica %d: fabricadas %d\n", i + 1, S.fabricadas_por_fabrica[i]);
        for (c = 0; c < CENTROS; c++) {
            log_print(&S, "  - Entregadas al centro %d: %d\n", c + 1, S.entregadas_por_fabrica[i][c]);
        }
    }

    for (c = 0; c < CENTROS; c++) {
        log_print(&S, "Centro %d:\n", c + 1);
        log_print(&S, "  - Vacunas recibidas (incluye iniciales): %d\n", S.recibidas_centro[c]);
        log_print(&S, "  - Habitantes vacunados: %d\n", S.vacunados_centro[c]);
        log_print(&S, "  - Vacunas sobrantes: %d\n", S.stock[c]);
    }

    /* Limpieza */
    if (S.fout) fclose(S.fout);

    for (c = 0; c < CENTROS; c++) {
        pthread_cond_destroy(&S.hay_vacunas[c]);
    }
    pthread_mutex_destroy(&S.mtx);
    pthread_mutex_destroy(&S.log_mtx);

    return 0;
}
