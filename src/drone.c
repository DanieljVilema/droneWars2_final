/**
 * drone.c
 * - 4 hilos: RX (comando), navegación, combustible, enlace.
 * - Enjambre = índice de objetivo (se pasa como argumento).
 * - En ABORTAR: finaliza inmediatamente (no detona/reporta).
 * - Telemetría mínima; VERBOSE=1 muestra eventos de enlace/abortar.
 * - Salida concisa: sólo "Detonó", "Reportó", "Sin combustible".
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <ctype.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <time.h>

#define CFG_PATH "config.txt"
#define MAX_STR  1024

typedef struct { int x,y; } Coordinate;

typedef struct {
    int num_camiones, num_objetivos;
    int prob_derribo, prob_perdida_com, tiempo_reestablecimiento;
    int combustible_inicial, radio_deteccion_art;
    int puerto_comando, puerto_base_artilleria;
    int verbose;
    Coordinate zonas_ensamble[128];  int zonas_count;
    Coordinate objetivos[128];       int obj_count;
    Coordinate camiones_pos[128];    int cam_pos_count;
} Config;

typedef enum { DRN_INACTIVO=0, DRN_DESPEGADO, DRN_EN_ORBITA, DRN_EN_RUTA, DRN_FINALIZADO } DState;

typedef struct {
    int id, tipo; // 0=ataque, 1=cámara
    int enjambre_id; // == target_idx
    int target_idx;
    Coordinate pos, zona, objetivo;
    int combustible;
    volatile int com_activa;
    volatile int permiso_despegar;
    volatile int enjambre_completo;
    volatile DState estado;
    int sock_cmd;
    pthread_mutex_t m;
} Drone;

static Drone DR; static Config CFG;

// ------- util -------
static void trim(char*s){ char*p=s; while(*p&&isspace((unsigned char)*p)) p++; if(p!=s) memmove(s,p,strlen(p)+1);
    size_t n=strlen(s); while(n>0&&isspace((unsigned char)s[n-1])) s[--n]='\0';}
static int parse_int(const char*s,int*out){ char*e=NULL; long v=strtol(s,&e,10); if(e==s||*e!='\0') return 0; *out=(int)v; return 1;}
static int parse_coords_list(const char*s,Coordinate*a,int cap){
    int cnt=0; char*dup=strdup(s); if(!dup) return 0; char*sv=NULL;
    for(char*t=strtok_r(dup,";",&sv); t&&cnt<cap; t=strtok_r(NULL,";",&sv)){
        trim(t); if(!*t) continue; char*c=strchr(t,','); if(!c) continue; *c='\0';
        char*sx=t,*sy=c+1; trim(sx); trim(sy); int x,y; if(!parse_int(sx,&x)||!parse_int(sy,&y)) continue;
        a[cnt].x=x; a[cnt].y=y; cnt++;
    } free(dup); return cnt;
}
static int load_config(Config*c){
    memset(c,0,sizeof(*c));
    c->num_camiones=2; c->num_objetivos=2; c->prob_perdida_com=5; c->tiempo_reestablecimiento=2;
    c->combustible_inicial=100; c->radio_deteccion_art=30; c->puerto_comando=8080; c->puerto_base_artilleria=9000; c->verbose=0;
    FILE*f=fopen(CFG_PATH,"r"); if(!f){ perror("drone:config"); return 0; }
    char line[MAX_STR];
    while(fgets(line,sizeof(line),f)){
        char*h=strchr(line,'#'); if(h)*h='\0'; trim(line); if(!*line) continue;
        char*e=strchr(line,'='); if(!e) continue; *e='\0'; char*k=line,*v=e+1; trim(k); trim(v);
        if(!strcmp(k,"NUM_CAMIONES")) parse_int(v,&c->num_camiones);
        else if(!strcmp(k,"NUM_OBJETIVOS")) parse_int(v,&c->num_objetivos);
        else if(!strcmp(k,"PROBABILIDAD_DERRIBO")) parse_int(v,&c->prob_derribo);
        else if(!strcmp(k,"PROBABILIDAD_PERDIDA_COM")) parse_int(v,&c->prob_perdida_com);
        else if(!strcmp(k,"TIEMPO_REESTABLECIMIENTO")) parse_int(v,&c->tiempo_reestablecimiento);
        else if(!strcmp(k,"COMBUSTIBLE_INICIAL")) parse_int(v,&c->combustible_inicial);
        else if(!strcmp(k,"RADIO_DETECCION_ARTILLERIA")) parse_int(v,&c->radio_deteccion_art);
        else if(!strcmp(k,"PUERTO_COMANDO")) parse_int(v,&c->puerto_comando);
        else if(!strcmp(k,"PUERTO_BASE_ARTILLERIA")) parse_int(v,&c->puerto_base_artilleria);
        else if(!strcmp(k,"ZONAS_ENSAMBLE")) c->zonas_count=parse_coords_list(v,c->zonas_ensamble,128);
        else if(!strcmp(k,"COORDENADAS_OBJETIVOS")) c->obj_count=parse_coords_list(v,c->objetivos,128);
        else if(!strcmp(k,"CAMIONES_POS")) c->cam_pos_count=parse_coords_list(v,c->camiones_pos,128);
        else if(!strcmp(k,"VERBOSE")) parse_int(v,&c->verbose);
    } fclose(f);
    if(c->obj_count<c->num_objetivos) c->obj_count=c->num_objetivos;
    if(c->zonas_count<c->num_objetivos) c->zonas_count=c->num_objetivos;
    if(c->cam_pos_count<c->num_camiones) c->cam_pos_count=c->num_camiones;
    return 1;
}
static int connect_local(int port){
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return -1;

    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port   = htons(port);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    socklen_t alen = sizeof(a);

    // Reintentos: 30 intentos x 100 ms = ~3s
    for (int i = 0; i < 30; i++) {
        if (connect(s, (struct sockaddr*)&a, alen) == 0) {
            return s;
        }
        usleep(100000); // 100 ms
    }
    // No se pudo conectar
    close(s);
    return -1;
}


static void send_critical(const char*m){ if(DR.sock_cmd>=0 && m && *m) send(DR.sock_cmd,m,(int)strlen(m),0); }
static void send_pos(const char*m){ if(!DR.com_activa) return; if(DR.sock_cmd>=0 && m&&*m) send(DR.sock_cmd,m,(int)strlen(m),0); }
static void report_pos(){ char b[96]; snprintf(b,sizeof(b),"POS:%d:%d_%d:%d",DR.id,DR.pos.x,DR.pos.y,DR.combustible); send_pos(b); }
static void notify_art(){
    // Notificar solo a la batería del objetivo actual
    int port = CFG.puerto_base_artilleria + DR.target_idx;
    int s = connect_local(port);
    if(s>=0){
        char m[96]; snprintf(m,sizeof(m),"DRONE_%d:POS_%d_%d",DR.id,DR.pos.x,DR.pos.y);
        send(s,m,(int)strlen(m),0);
        close(s);
    }
}
static int manhattan(Coordinate a,Coordinate b){ return abs(a.x-b.x)+abs(a.y-b.y); }

// ------- hilos -------
static void* th_rx(void*arg){
    (void)arg;
    char reg[96]; snprintf(reg,sizeof(reg),"REGISTRO:%d:%d:%d",DR.id,DR.tipo,DR.enjambre_id); send_critical(reg);
    char buf[128];
    for(;;){
        int n=recv(DR.sock_cmd,buf,sizeof(buf)-1,0); if(n<=0) break; buf[n]='\0';
        if(!strncmp(buf,"DESPEGAR",8)){
            int idx=-1; char *c=strchr(buf,':'); if(c) idx=atoi(c+1);
            pthread_mutex_lock(&DR.m);
            DR.permiso_despegar=1; if(idx>=0 && idx<CFG.num_objetivos){ DR.target_idx=idx; DR.enjambre_id=idx; DR.objetivo=CFG.objetivos[idx]; DR.zona=CFG.zonas_ensamble[idx]; }
            if(DR.estado==DRN_INACTIVO) DR.estado=DRN_DESPEGADO;
            pthread_mutex_unlock(&DR.m);
        } else if(!strncmp(buf,"ENJAMBRE_COMPLETO",17)){
            int salio = 0;
            pthread_mutex_lock(&DR.m);
            DR.enjambre_completo=1;
            if(DR.estado==DRN_EN_ORBITA){ DR.estado=DRN_EN_RUTA; salio=1; }
            pthread_mutex_unlock(&DR.m);
            if(salio){
                char e[64]; snprintf(e,sizeof(e),"EVT:%d:SALIENDO_ENSAMBLE",DR.id);
                send_critical(e);
            }
        } else if(!strncmp(buf,"REASIGNAR:",10)){
            int new_enj=atoi(buf+10);
            pthread_mutex_lock(&DR.m);
            DR.enjambre_id=new_enj; DR.target_idx=new_enj; DR.zona=CFG.zonas_ensamble[new_enj]; DR.objetivo=CFG.objetivos[new_enj];
            DR.enjambre_completo=0; if(DR.estado!=DRN_FINALIZADO) DR.estado=DRN_DESPEGADO;
            pthread_mutex_unlock(&DR.m);
        } else if(!strncmp(buf,"ABORTAR",7)){
            // Orden de abortar (derribado o pérdida definitiva)
            pthread_mutex_lock(&DR.m);
            if(CFG.verbose) fprintf(stdout,"[DRN %d] Abortado\n", DR.id);
            DR.estado = DRN_FINALIZADO;
            pthread_mutex_unlock(&DR.m);
            break; // corta recepción
        }
    }
    return NULL;
}

static void orbitar(Coordinate c,int paso){
    static int f; switch(f){ case 0: DR.pos.x+=2; if(DR.pos.x>=c.x+paso) f=1; break;
        case 1: DR.pos.y+=2; if(DR.pos.y>=c.y+paso) f=2; break;
        case 2: DR.pos.x-=2; if(DR.pos.x<=c.x-paso) f=3; break;
        case 3: DR.pos.y-=2; if(DR.pos.y<=c.y-paso) f=0; break; }
}
static void avanzar(Coordinate d){
    if(DR.pos.x<d.x) DR.pos.x+=2; else if(DR.pos.x>d.x) DR.pos.x-=2;
    if(DR.pos.y<d.y) DR.pos.y+=2; else if(DR.pos.y>d.y) DR.pos.y-=2;
}

static void* th_nav(void*arg){
    (void)arg;
    while(!DR.permiso_despegar && DR.estado!=DRN_FINALIZADO) usleep(80000);
    while(DR.estado!=DRN_FINALIZADO && DR.estado!=DRN_EN_ORBITA){
        pthread_mutex_lock(&DR.m);
        if(DR.estado==DRN_DESPEGADO){
            avanzar(DR.zona);
            if(manhattan(DR.pos,DR.zona)<=2){
                DR.estado=DRN_EN_ORBITA;
                char e[64]; snprintf(e,sizeof(e),"EVT:%d:LLEGUE_ENSAMBLE",DR.id); pthread_mutex_unlock(&DR.m);
                send_critical(e); usleep(60000); continue;
            }
        }
        pthread_mutex_unlock(&DR.m);
        report_pos(); notify_art(); usleep(100000);
    }
    while(DR.estado==DRN_EN_ORBITA && DR.estado!=DRN_FINALIZADO){
        int should_leave = 0;
        pthread_mutex_lock(&DR.m);
        if(DR.enjambre_completo && DR.estado==DRN_EN_ORBITA){
            DR.estado = DRN_EN_RUTA;
            should_leave = 1;
        }
        pthread_mutex_unlock(&DR.m);
        if(should_leave){
            char e[64]; snprintf(e,sizeof(e),"EVT:%d:SALIENDO_ENSAMBLE",DR.id);
            send_critical(e);
            // cae al bucle EN_RUTA en la siguiente iteración del bucle general
            break;
        }
        pthread_mutex_lock(&DR.m);
        orbitar(DR.zona,6);
        pthread_mutex_unlock(&DR.m);
        report_pos(); notify_art(); usleep(100000);
    }
    while(DR.estado==DRN_EN_RUTA && DR.estado!=DRN_FINALIZADO){
        pthread_mutex_lock(&DR.m); avanzar(DR.objetivo);
        int cerca=(manhattan(DR.pos,DR.objetivo)<=2); pthread_mutex_unlock(&DR.m);
        report_pos(); notify_art();
        if(cerca){
            if(DR.tipo==0){ char e[48]; snprintf(e,sizeof(e),"EVT:%d:DETONACION",DR.id); send_critical(e); printf("[DRN %d] Detonó\n",DR.id); }
            else          { char e[64]; snprintf(e,sizeof(e),"EVT:%d:OBJETIVO_REPORTADO",DR.id); send_critical(e); printf("[DRN %d] Reportó\n",DR.id); }
            usleep(80000); pthread_mutex_lock(&DR.m); DR.estado=DRN_FINALIZADO; pthread_mutex_unlock(&DR.m); break;
        }
        usleep(80000);
    }
    return NULL;
}

static void* th_comb(void*arg){
    (void)arg;
    while(DR.estado!=DRN_FINALIZADO){
        usleep(280000);
        pthread_mutex_lock(&DR.m);
        int en_mov=(DR.estado==DRN_DESPEGADO||DR.estado==DRN_EN_ORBITA||DR.estado==DRN_EN_RUTA);
        if(en_mov && DR.combustible>0) DR.combustible--;
        int sin=(DR.combustible<=0); pthread_mutex_unlock(&DR.m);
        if(sin){
            char e[64]; snprintf(e,sizeof(e),"EVT:%d:SIN_COMBUSTIBLE",DR.id); send_critical(e); printf("[DRN %d] Sin combustible\n",DR.id);
            usleep(40000); pthread_mutex_lock(&DR.m); DR.estado=DRN_FINALIZADO; pthread_mutex_unlock(&DR.m); break;
        }
    } return NULL;
}

static void* th_link(void*arg){
    (void)arg; srand((unsigned)time(NULL)^DR.id);
    while(DR.estado!=DRN_FINALIZADO){
        sleep(2);
        if(rand()%100 < CFG.prob_perdida_com){
            pthread_mutex_lock(&DR.m); DR.com_activa=0; pthread_mutex_unlock(&DR.m);
            if(CFG.verbose) fprintf(stdout,"[DRN %d] COM_PERDIDA\n", DR.id);
            char e1[48]; snprintf(e1,sizeof(e1),"EVT:%d:COM_PERDIDA",DR.id); send_critical(e1);

            sleep(CFG.tiempo_reestablecimiento);
            if(rand()%100 < 50){
                pthread_mutex_lock(&DR.m); DR.com_activa=1; pthread_mutex_unlock(&DR.m);
                if(CFG.verbose) fprintf(stdout,"[DRN %d] COM_RESTABLECIDA\n", DR.id);
                char e2[64]; snprintf(e2,sizeof(e2),"EVT:%d:COM_RESTABLECIDA",DR.id); send_critical(e2);
            } else {
                if(CFG.verbose) fprintf(stdout,"[DRN %d] COM_PERDIDA_TIMEOUT → ABORT\n", DR.id);
                pthread_mutex_lock(&DR.m); DR.estado=DRN_FINALIZADO; pthread_mutex_unlock(&DR.m);
                // también avisamos al comando implícitamente por la desconexión
                break;
            }
        }
    } return NULL;
}

// ------- main -------
int main(int argc,char**argv){
    if(argc!=4){
        fprintf(stderr,"Uso: %s <drone_id> <tipo(0=ataque,1=camara)> <objetivo_idx>\n",argv[0]); return 1;
    }
    if(!load_config(&CFG)) return 1;

    DR.id=atoi(argv[1]); DR.tipo=atoi(argv[2]); DR.target_idx=atoi(argv[3]);
    if(DR.target_idx<0 || DR.target_idx>=CFG.num_objetivos) DR.target_idx=DR.id % CFG.num_objetivos;
    DR.enjambre_id = DR.target_idx;

    int cam_aprox = (DR.id/100);
    DR.pos = (cam_aprox<CFG.cam_pos_count)? CFG.camiones_pos[cam_aprox] : (Coordinate){10+cam_aprox*30,10+cam_aprox*20};
    DR.zona = (DR.target_idx<CFG.zonas_count)? CFG.zonas_ensamble[DR.target_idx] : (Coordinate){50+DR.target_idx*20,100};
    DR.objetivo = CFG.objetivos[DR.target_idx];

    DR.combustible=CFG.combustible_inicial; DR.com_activa=1; DR.permiso_despegar=0; DR.enjambre_completo=0; DR.estado=DRN_INACTIVO;
    pthread_mutex_init(&DR.m,NULL);

    DR.sock_cmd = connect_local(CFG.puerto_comando);
    if(DR.sock_cmd<0){ perror("drone:connect comando"); return 1; }

    pthread_t rx,nav,comb,link;
    pthread_create(&rx,NULL,th_rx,NULL);
    pthread_create(&nav,NULL,th_nav,NULL);
    pthread_create(&comb,NULL,th_comb,NULL);
    pthread_create(&link,NULL,th_link,NULL);

    pthread_join(nav,NULL);
    pthread_cancel(rx); pthread_join(rx,NULL);
    pthread_cancel(link); pthread_join(link,NULL);
    pthread_cancel(comb); pthread_join(comb,NULL);

    close(DR.sock_cmd);
    pthread_mutex_destroy(&DR.m);
    return 0;
}
