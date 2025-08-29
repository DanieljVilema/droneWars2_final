/*
 * Sistema de Artilleria Anti-Aerea - artilleria.c
 * Curso: Sistemas Operativos
 * 
 * Cada objetivo tiene su propio sistema de defensa:
 * - Detecta drones que entran al radio de defensa
 * - Intenta derribarlos segun probabilidad configurada
 * - Notifica derribos al centro de comando
 * - Evita reportar el mismo derribo varias veces
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <time.h>

#define CFG_PATH "config.txt"
#define MAX_STR  1024
#define MAX_DRONES 2048

typedef struct { int x,y; } Coordinate;
typedef struct {
    int puerto_cmd, puerto_base_art, prob_derribo, radio, num_obj, verbose;
    Coordinate objetivos[128]; int obj_count;
} Config;

static int ART_ID=-1; static Config CFG; static Coordinate POS_ME;
// bitset simple de derribos
static unsigned char knocked[(MAX_DRONES+7)/8];
// estado de "dentro del radio" por dron para intentar solo al entrar
static unsigned char inrad[MAX_DRONES];

static void set_knocked(int id){ knocked[id>>3] |= (1u<<(id&7)); }
static int  is_knocked(int id){ return (knocked[id>>3] & (1u<<(id&7))) != 0; }

static void trim(char*s){ char*p=s; while(*p&&isspace((unsigned char)*p)) p++; if(p!=s) memmove(s,p,strlen(p)+1);
    size_t n=strlen(s); while(n>0&&isspace((unsigned char)s[n-1])) s[--n]='\0';}
static int parse_int(const char*s,int*out){ char*e=NULL; long v=strtol(s,&e,10); if(e==s||*e!='\0') return 0; *out=(int)v; return 1;}
static int parse_coords_list(const char*s, Coordinate*a, int cap){
    int cnt = 0;
    char *dup = strdup(s);
    if (!dup) return 0;
    char *sv = NULL;

    for (char *t = strtok_r(dup, ";", &sv); t && cnt < cap; t = strtok_r(NULL, ";", &sv)) {
        trim(t);
        if (!*t) continue;

        char *c = strchr(t, ',');
        if (!c) continue;

        *c = '\0';
        char *sx = t;
        char *sy = c + 1;
        trim(sx);
        trim(sy);

        int x, y;
        if (!parse_int(sx, &x) || !parse_int(sy, &y)) {
            continue;
        }

        a[cnt].x = x;
        a[cnt].y = y;
        cnt++;
    }

    free(dup);
    return cnt;
}

static int load_config(Config*c){
    memset(c,0,sizeof(*c)); c->puerto_cmd=8080; c->puerto_base_art=9000; c->prob_derribo=15; c->radio=30; c->num_obj=2; c->verbose=0;
    FILE*f=fopen(CFG_PATH,"r"); if(!f){ perror("art:config"); return 0; }
    char line[MAX_STR];
    while(fgets(line,sizeof(line),f)){
        char*h=strchr(line,'#'); if(h)*h='\0'; trim(line); if(!*line) continue;
        char*e=strchr(line,'='); if(!e) continue; *e='\0'; char*k=line,*v=e+1; trim(k); trim(v);
        if(!strcmp(k,"PUERTO_COMANDO")) parse_int(v,&c->puerto_cmd);
        else if(!strcmp(k,"PUERTO_BASE_ARTILLERIA")) parse_int(v,&c->puerto_base_art);
        else if(!strcmp(k,"PROBABILIDAD_DERRIBO")) parse_int(v,&c->prob_derribo);
        else if(!strcmp(k,"RADIO_DETECCION_ARTILLERIA")) parse_int(v,&c->radio);
        else if(!strcmp(k,"NUM_OBJETIVOS")) parse_int(v,&c->num_obj);
        else if(!strcmp(k,"COORDENADAS_OBJETIVOS")) c->obj_count=parse_coords_list(v,c->objetivos,128);
        else if(!strcmp(k,"VERBOSE")) parse_int(v,&c->verbose);
    } fclose(f);
    if(c->obj_count<c->num_obj) c->obj_count=c->num_obj;
    return 1;
}
static int open_server(int port){
    int s=socket(AF_INET,SOCK_STREAM,0); if(s<0) return -1;
    int opt=1; setsockopt(s,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(opt));
    struct sockaddr_in a; memset(&a,0,sizeof(a));
    a.sin_family=AF_INET; a.sin_addr.s_addr=INADDR_ANY; a.sin_port=htons(port);
    if(bind(s,(struct sockaddr*)&a,sizeof(a))<0){ close(s); return -1; }
    if(listen(s,32)<0){ close(s); return -1; }
    return s;
}
static int manhattan(Coordinate a,Coordinate b){ return abs(a.x-b.x)+abs(a.y-b.y); }
static void notify_cmd_derribo(int drone_id){
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return;

    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port   = htons(CFG.puerto_cmd);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    socklen_t alen = sizeof(a);
    if (connect(s, (struct sockaddr*)&a, alen) == 0) {
        char m[64];
        snprintf(m, sizeof(m), "EVT_DERRIBO:%d", drone_id);
        send(s, m, (int)strlen(m), 0);
    }
    close(s);
}

int main(int argc,char**argv){
    if(argc!=2){ fprintf(stderr,"Uso: %s <art_id>\n",argv[0]); return 1; }
    ART_ID=atoi(argv[1]); if(!load_config(&CFG)) return 1;
    if(ART_ID<0||ART_ID>=CFG.num_obj){ fprintf(stderr,"art: id fuera de rango\n"); return 1; }
    POS_ME=CFG.objetivos[ART_ID];
    memset(knocked,0,sizeof(knocked));
    memset(inrad,0,sizeof(inrad));

    int port=CFG.puerto_base_art+ART_ID;
    int srv=open_server(port); if(srv<0){ perror("art:listen"); return 1; }

    srand((unsigned)time(NULL)^ART_ID);
    for(;;){
        int cs=accept(srv,NULL,NULL); if(cs<0) continue;
        char buf[256]; int n=recv(cs,buf,sizeof(buf)-1,0);
        if(n>0){
            buf[n]='\0';
            int id,x,y; if(sscanf(buf,"DRONE_%d:POS_%d_%d",&id,&x,&y)==3){
                if(id>=0 && id<MAX_DRONES && !is_knocked(id)){
                    Coordinate p={x,y};
                    int inside = (manhattan(p,POS_ME) <= CFG.radio);
                    if(inside){
                        if(!inrad[id]){
                            inrad[id] = 1; // primer ingreso, un solo intento
                            if(rand()%100 < CFG.prob_derribo){
                                set_knocked(id);
                                notify_cmd_derribo(id);
                                // Derribo silencioso - no mostrar mensaje
                            }
                        }
                    } else {
                        inrad[id] = 0; // salió, habilitar intento en próxima entrada
                    }
                }
            }
        }
        close(cs);
    }
    close(srv); return 0;
}
