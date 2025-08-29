/*
 * Proyecto DroneWars2 - Archivo Principal
 * Curso: Sistemas Operativos
 * 
 * Este programa lanza todos los procesos del simulador:
 * - Centro de comando (coordina enjambres)
 * - Sistemas de artilleria (uno por objetivo)
 * - Camiones (lanzan drones)
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>

#define CFG_PATH "config.txt"
#define MAX_STR 1024
#define MAXN 128

typedef struct
{
    int x, y;
} Coordinate;

typedef struct
{
    int num_camiones, num_objetivos;
    int prob_derribo, prob_perdida, z_reest;
    int comb_inicial, radio_aa;
    int puerto_cmd, puerto_base_art;
    int verbose;
    Coordinate camiones_pos[MAXN];
    int cam_pos_count;
    Coordinate zonas_ensamble[MAXN];
    int zonas_count;
    Coordinate objetivos[MAXN];
    int obj_count;
} Config;

// Variables globales para controlar los procesos
static pid_t proceso_comando = -1;
static pid_t procesos_artilleria[MAXN];
static pid_t procesos_camiones[MAXN];
static int num_sistemas_artilleria = 0, num_camiones = 0;
static volatile sig_atomic_t simulacion_interrumpida = 0;

static void trim(char *s)
{
    char *p = s;
    while (*p && isspace((unsigned char)*p))
        p++;
    if (p != s)
    {
        memmove(s, p, strlen(p) + 1);
    }
    size_t n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1]))
    {
        s[--n] = '\0';
    }
}
static int parse_int(const char *s, int *out)
{
    char *e = NULL;
    long v = strtol(s, &e, 10);
    if (e == s || *e != '\0')
    {
        return 0;
    }
    *out = (int)v;
    return 1;
}
static int parse_coords_list(const char *s, Coordinate *arr, int cap)
{
    int cnt = 0;
    char *dup = strdup(s);
    if (!dup)
        return 0;
    char *sv = NULL;
    for (char *t = strtok_r(dup, ";", &sv); t && cnt < cap; t = strtok_r(NULL, ";", &sv))
    {
        trim(t);
        if (!*t)
            continue;
        char *c = strchr(t, ',');
        if (!c)
            continue;
        *c = '\0';
        char *sx = t, *sy = c + 1;
        trim(sx);
        trim(sy);
        int x, y;
        if (!parse_int(sx, &x) || !parse_int(sy, &y))
            continue;
        arr[cnt].x = x;
        arr[cnt].y = y;
        cnt++;
    }
    free(dup);
    return cnt;
}
static int load_config(Config *c)
{
    memset(c, 0, sizeof(*c));
    c->num_camiones = 2;
    c->num_objetivos = 2;
    c->prob_derribo = 15;
    c->prob_perdida = 5;
    c->z_reest = 2;
    c->comb_inicial = 100;
    c->radio_aa = 30;
    c->puerto_cmd = 8080;
    c->puerto_base_art = 9000;
    c->verbose = 0;
    FILE *f = fopen(CFG_PATH, "r");
    if (!f)
    {
        perror("main: config.txt");
        return 0;
    }
    char line[MAX_STR];
    while (fgets(line, sizeof(line), f))
    {
        char *h = strchr(line, '#');
        if (h)
            *h = '\0';
        trim(line);
        if (!*line)
            continue;
        char *eq = strchr(line, '=');
        if (!eq)
            continue;
        *eq = '\0';
        char *k = line, *v = eq + 1;
        trim(k);
        trim(v);
        if (!strcmp(k, "NUM_CAMIONES"))
            parse_int(v, &c->num_camiones);
        else if (!strcmp(k, "NUM_OBJETIVOS"))
            parse_int(v, &c->num_objetivos);
        else if (!strcmp(k, "PROBABILIDAD_DERRIBO"))
            parse_int(v, &c->prob_derribo);
        else if (!strcmp(k, "PROBABILIDAD_PERDIDA_COM"))
            parse_int(v, &c->prob_perdida);
        else if (!strcmp(k, "TIEMPO_REESTABLECIMIENTO"))
            parse_int(v, &c->z_reest);
        else if (!strcmp(k, "COMBUSTIBLE_INICIAL"))
            parse_int(v, &c->comb_inicial);
        else if (!strcmp(k, "RADIO_DETECCION_ARTILLERIA"))
            parse_int(v, &c->radio_aa);
        else if (!strcmp(k, "PUERTO_COMANDO"))
            parse_int(v, &c->puerto_cmd);
        else if (!strcmp(k, "PUERTO_BASE_ARTILLERIA"))
            parse_int(v, &c->puerto_base_art);
        else if (!strcmp(k, "CAMIONES_POS"))
            c->cam_pos_count = parse_coords_list(v, c->camiones_pos, MAXN);
        else if (!strcmp(k, "ZONAS_ENSAMBLE"))
            c->zonas_count = parse_coords_list(v, c->zonas_ensamble, MAXN);
        else if (!strcmp(k, "COORDENADAS_OBJETIVOS"))
            c->obj_count = parse_coords_list(v, c->objetivos, MAXN);
        else if (!strcmp(k, "VERBOSE"))
            parse_int(v, &c->verbose);
    }
    fclose(f);
    return 1;
}

static void on_sigint(int sig)
{
    (void)sig;
    simulacion_interrumpida = 1;
}

static pid_t launch(const char *path, char *const argv[])
{
    pid_t p = fork();
    if (p < 0)
    {
        perror("fork");
        return -1;
    }
    if (p == 0)
    {
        execv(path, argv);
        perror("execv");
        _exit(127);
    }
    return p;
}

int main(void)
{
    signal(SIGINT, on_sigint);
    Config C;
    if (!load_config(&C))
        return 1;

    printf("== DRONEWARS2 ==\n");
    printf("Camiones=%d | Objetivos=%d | W=%d%% | Q=%d%% | Z=%ds | Fuel=%d | RadioAA=%d\n",
           C.num_camiones, C.num_objetivos, C.prob_derribo, C.prob_perdida, C.z_reest, C.comb_inicial, C.radio_aa);

    // COMANDO - Lanzar el centro de comando
    {
        char *const av[] = {"./comando", NULL};
        proceso_comando = launch("./comando", av);
        if (proceso_comando < 0)
            return 1;
        usleep(200000); // 200 ms para que el comando quede escuchando
    }

    // ARTILLERIAS - Una por cada objetivo
    num_sistemas_artilleria = C.num_objetivos;
    for (int i = 0; i < num_sistemas_artilleria; i++)
    {
        char idb[16];
        snprintf(idb, sizeof(idb), "%d", i);
        char *const av[] = {"./artilleria", idb, NULL};
        procesos_artilleria[i] = launch("./artilleria", av);
    }

    // CAMIONES - Lanzar todos los camiones
    num_camiones = C.num_camiones;
    for (int i = 0; i < num_camiones; i++)
    {
        char idb[16];
        snprintf(idb, sizeof(idb), "%d", i);
        char *const av[] = {"./camion", idb, NULL};
        procesos_camiones[i] = launch("./camion", av);
    }

    // Esperar a que el COMANDO termine o recibir Ctrl+C
    int status = 0;
    while (!simulacion_interrumpida)
    {
        pid_t r = waitpid(proceso_comando, &status, WNOHANG);
        if (r == proceso_comando)
            break;
        usleep(200000);
    }

    // Terminar todos los procesos restantes
    for (int i = 0; i < num_camiones; i++)
        if (procesos_camiones[i] > 0)
            kill(procesos_camiones[i], SIGTERM);
    for (int i = 0; i < num_sistemas_artilleria; i++)
        if (procesos_artilleria[i] > 0)
            kill(procesos_artilleria[i], SIGTERM);
    for (int i = 0; i < num_camiones; i++)
        if (procesos_camiones[i] > 0)
            waitpid(procesos_camiones[i], NULL, 0);
    for (int i = 0; i < num_sistemas_artilleria; i++)
        if (procesos_artilleria[i] > 0)
            waitpid(procesos_artilleria[i], NULL, 0);
    if (proceso_comando > 0)
        waitpid(proceso_comando, NULL, 0);

    printf("Simulación finalizada.\n");
    return 0;
}
