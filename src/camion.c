/*
 * Programa del CAMION - camion.c
 * Curso: Sistemas Operativos
 * 
 * Cada camion lanza sus drones segun la configuracion:
 * - Lee la cantidad de drones por camion del archivo config
 * - Cada drone elige un objetivo aleatorio para despistar al enemigo
 * - Usa fork() y execv() para crear procesos drone
 * - Espera a que terminen todos sus drones antes de finalizar
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <ctype.h>
#include <time.h>

#define CFG_PATH "config.txt"

typedef struct { int x,y; } Coordinate;
typedef struct {
    int num_camiones, num_objetivos, puerto_comando;
    Coordinate camiones_pos[128]; int camiones_pos_count;
    char cargas_raw[1024]; int verbose;
} Config;

static void trim(char *s) {
    char *p = s;
    while (*p && isspace((unsigned char)*p)) p++;
    if (p != s) {
        memmove(s, p, strlen(p) + 1);
    }
    size_t n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1])) {
        s[--n] = '\0';
    }
}
static int parse_int(const char *s, int *out) {
    char *e = NULL;
    long v = strtol(s, &e, 10);
    if (e == s || *e != '\0') {
        return 0;
    }
    *out = (int)v;
    return 1;
}
static int parse_coords_list(const char*s,Coordinate*a,int cap){
    int cnt=0; char*dup=strdup(s); if(!dup) return 0; char*sv=NULL;
    for(char*t=strtok_r(dup,";",&sv); t&&cnt<cap; t=strtok_r(NULL,";",&sv)){
        trim(t); if(!*t) continue; char*c=strchr(t,','); if(!c) continue; *c='\0';
        char*sx=t,*sy=c+1; trim(sx); trim(sy); int x,y;
        if(!parse_int(sx,&x)||!parse_int(sy,&y)) continue;
        a[cnt].x=x; a[cnt].y=y; cnt++;
    } free(dup); return cnt;
}
static int load_config(Config *c){
    memset(c,0,sizeof(*c)); c->num_camiones=2; c->num_objetivos=2; c->puerto_comando=8080; c->verbose=0;
    FILE*f=fopen(CFG_PATH,"r"); if(!f){ perror("camion:config"); return 0; }
    char line[1024];
    while(fgets(line,sizeof(line),f)){
        char*h=strchr(line,'#'); if(h)*h='\0'; trim(line); if(!*line) continue;
        char*e=strchr(line,'='); if(!e) continue; *e='\0'; char*k=line,*v=e+1; trim(k); trim(v);
        if(!strcmp(k,"NUM_CAMIONES")) parse_int(v,&c->num_camiones);
        else if(!strcmp(k,"NUM_OBJETIVOS")) parse_int(v,&c->num_objetivos);
        else if(!strcmp(k,"PUERTO_COMANDO")) parse_int(v,&c->puerto_comando);
        else if(!strcmp(k,"CAMIONES_POS")) c->camiones_pos_count=parse_coords_list(v,c->camiones_pos,128);
        else if(!strcmp(k,"CAMION_CARGAS")) { strncpy(c->cargas_raw,v,sizeof(c->cargas_raw)-1); c->cargas_raw[sizeof(c->cargas_raw)-1]='\0'; }
        else if(!strcmp(k,"VERBOSE")) parse_int(v,&c->verbose);
    } fclose(f);
    return 1;
}
static void parse_carga(const char *spec, int *atk, int *cam){
    *atk=0; *cam=0; if(!spec||!*spec) return;
    int a=0,c=0; const char *p=spec;
    while(isdigit((unsigned char)*p)){ a = a*10 + (*p - '0'); p++; }
    if(*p=='A') p++;
    if(*p){
        int n=0; while(isdigit((unsigned char)*p)){ n = n*10 + (*p - '0'); p++; }
        if(*p=='C') c = (n>0?n:1);
    }
    *atk=a; *cam=c;
}
int main(int argc, char **argv){
    if(argc!=2){ fprintf(stderr,"Uso: %s <camion_id>\n",argv[0]); return 1; }
    int camion_id=atoi(argv[1]);
    Config C; if(!load_config(&C)) return 1;
    if(camion_id<0||camion_id>=C.num_camiones){ fprintf(stderr,"camion: id fuera de rango\n"); return 1; }

    int atk=0, cam=0;
    {
        char *dup=strdup(C.cargas_raw), *sv=NULL; int idx=0; char *tok=NULL;
        for(tok=strtok_r(dup,";",&sv); tok; tok=strtok_r(NULL,";",&sv)){
            if(idx==camion_id){ trim(tok); parse_carga(tok,&atk,&cam); break; }
            idx++;
        } free(dup);
    }
    if(atk==0 && cam==0){ atk=4; cam=1; } // default sensato

    printf("[CAMION %d] Carga: %dA + %dC\n", camion_id, atk, cam);

    srand((unsigned)time(NULL) ^ camion_id);
    int total = atk + cam;
    pid_t *pids = calloc(total, sizeof(pid_t));
    if(!pids) { perror("calloc"); return 1; }

    int launched=0;
    for(int i=0;i<atk;i++){
        int drone_id = camion_id*100 + i; int tipo = 0; int objetivo = rand()%C.num_objetivos;
        pid_t p=fork(); if(p<0){ perror("fork"); return 1; }
        if(p==0){
            char idb[16],tb[8],ob[16];
            snprintf(idb,sizeof(idb),"%d",drone_id); snprintf(tb,sizeof(tb),"%d",tipo); snprintf(ob,sizeof(ob),"%d",objetivo);
            char *const av[]={"./drone", idb, tb, ob, NULL}; execv("./drone", av); perror("execv drone"); _exit(127);
        } else { pids[launched++]=p; printf("[C%d] DRN %d ATAQUE → OBJ %d\n", camion_id, drone_id, objetivo); }
    }
    for(int i=0;i<cam;i++){
        int drone_id = camion_id*100 + atk + i; int tipo = 1; int objetivo = rand()%C.num_objetivos;
        pid_t p=fork(); if(p<0){ perror("fork"); return 1; }
        if(p==0){
            char idb[16],tb[8],ob[16];
            snprintf(idb,sizeof(idb),"%d",drone_id); snprintf(tb,sizeof(tb),"%d",tipo); snprintf(ob,sizeof(ob),"%d",objetivo);
            char *const av[]={"./drone", idb, tb, ob, NULL}; execv("./drone", av); perror("execv drone"); _exit(127);
        } else { pids[launched++]=p; printf("[C%d] DRN %d CAMARA → OBJ %d\n", camion_id, drone_id, objetivo); }
    }

    // Espera silenciosa (no spameamos finalizaciones individuales)
    for(int i=0;i<launched;i++){ int st=0; waitpid(pids[i],&st,0); }
    free(pids);
    return 0;
}
