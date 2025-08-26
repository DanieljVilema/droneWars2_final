/**
 * comando.c
 * - Enjambres indexados por OBJETIVO (no por camión).
 * - COMPLETO = exactamente (>=4 ataque) y (>=1 cámara) en zona de ensamble.
 * - Re-ensamblaje alternado izq-der, priorizando el TIPO que falta (primero cámara, luego ataque).
 * - Al derribo: deduplicado en AA, aquí se envía ABORTAR al dron y se marca finalizado.
 * - Salida concisa: COMPLETO, Impacto/Reporte, Resumen final.
 * - VERBOSE=1 (config.txt) muestra trazas de re-ensamble y link si hiciera falta.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <time.h>

#define CFG_PATH "config.txt"
#define MAX_STR  1024
#define MAX_DRONES 2048
#define DRONES_POR_ENJAMBRE 5
#define MAX_ENJ 256
#define REASSIGN_COOLDOWN 3   // s
#define PAIR_COOLDOWN     5   // s
#define SIM_TIMEOUT      60   // s

typedef struct { int x,y; } Coordinate;

typedef struct {
    int num_camiones, num_objetivos, puerto_comando, verbose;
    Coordinate objetivos[MAX_ENJ]; int obj_count;
} Config;

typedef struct {
    int id, tipo;              // 0 ataque, 1 camara
    int enjambre_id;
    int sock, conectado;
    int en_ensamble;           // llegó a la zona
    int detono, reporto;
    int finalizado, derribado;
    int comb;
    Coordinate pos;
    time_t last_msg;
    time_t last_moved_ts;      // cooldown re-ensamblaje por dron
} Drone;

typedef struct {
    int id;
    // Conteos en zona de ensamble por tipo
    int ens_attack;            // cuántos ATAQUE están en zona
    int ens_camera;            // cuántos CAMARA están en zona
    int completos;             // 1 si 4A+1C
    int activos;               // conectados y no finalizados
    int detonaciones;
    int camara_reporto;
    pthread_mutex_t lock;
    time_t last_reassign_ts;
    int last_donor; time_t last_donor_ts;
} Enjambre;

typedef struct {
    int id; Coordinate pos;
    int estado;    // 0 intacto, 1 parcial, 2 total
    int impactos;  // detonaciones (ataque) sobre el blanco
} Blanco;

static Config CFG;
static int srv_sock=-1;
static Drone   DR[MAX_DRONES];
static Enjambre ENJ[MAX_ENJ];
static Blanco   BL[MAX_ENJ];
static time_t SIM_START;
static time_t last_pair_move[MAX_ENJ][MAX_ENJ]; // anti ping-pong

// ------------- util -------------
static void trim(char*s){ char*p=s; while(*p&&isspace((unsigned char)*p)) p++; if(p!=s) memmove(s,p,strlen(p)+1);
    size_t n=strlen(s); while(n>0&&isspace((unsigned char)s[n-1])) s[--n]='\0';}
static int parse_int(const char*s,int*out){ char*e=NULL; long v=strtol(s,&e,10); if(e==s||*e!='\0') return 0; *out=(int)v; return 1; }
static int parse_coords_list(const char*s,Coordinate*a,int cap){
    int cnt=0; char*dup=strdup(s); if(!dup) return 0; char*sv=NULL;
    for(char*t=strtok_r(dup,";",&sv); t&&cnt<cap; t=strtok_r(NULL,";",&sv)){
        trim(t); if(!*t) continue; char*c=strchr(t,','); if(!c) continue; *c='\0';
        char*sx=t,*sy=c+1; trim(sx); trim(sy); int x,y;
        if(!parse_int(sx,&x)||!parse_int(sy,&y)) continue;
        a[cnt].x=x; a[cnt].y=y; cnt++;
    } free(dup); return cnt;
}
static int load_config(Config*c){
    memset(c,0,sizeof(*c)); c->num_camiones=2; c->num_objetivos=2; c->puerto_comando=8080; c->verbose=0;
    FILE*f=fopen(CFG_PATH,"r"); if(!f){ perror("comando:config"); return 0; }
    char line[MAX_STR];
    while(fgets(line,sizeof(line),f)){
        char*h=strchr(line,'#'); if(h)*h='\0'; trim(line); if(!*line) continue;
        char*e=strchr(line,'='); if(!e) continue; *e='\0'; char*k=line,*v=e+1; trim(k); trim(v);
        if(!strcmp(k,"NUM_CAMIONES")) parse_int(v,&c->num_camiones);
        else if(!strcmp(k,"NUM_OBJETIVOS")) parse_int(v,&c->num_objetivos);
        else if(!strcmp(k,"PUERTO_COMANDO")) parse_int(v,&c->puerto_comando);
        else if(!strcmp(k,"COORDENADAS_OBJETIVOS")) c->obj_count=parse_coords_list(v,c->objetivos,MAX_ENJ);
        else if(!strcmp(k,"VERBOSE")) parse_int(v,&c->verbose);
    }
    fclose(f);
    if(c->obj_count<c->num_objetivos) c->obj_count=c->num_objetivos;
    return 1;
}
static int open_server(int port){
    int s=socket(AF_INET,SOCK_STREAM,0); if(s<0) return -1;
    int opt=1; setsockopt(s,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(opt));
    struct sockaddr_in a; memset(&a,0,sizeof(a));
    a.sin_family=AF_INET; a.sin_addr.s_addr=INADDR_ANY; a.sin_port=htons(port);
    if(bind(s,(struct sockaddr*)&a,sizeof(a))<0){ close(s); return -1; }
    if(listen(s,256)<0){ close(s); return -1; }
    return s;
}
static void send_to_drone(int sock,const char*cmd){ if(sock>=0 && cmd && *cmd) send(sock,cmd,(int)strlen(cmd),0); }

// ------------- estado -------------
static void init_state(void){
    for(int i=0;i<MAX_DRONES;i++){ DR[i].id=-1; DR[i].sock=-1; }
    for(int i=0;i<CFG.num_objetivos;i++){
        ENJ[i].id=i; ENJ[i].ens_attack=0; ENJ[i].ens_camera=0; ENJ[i].completos=0;
        ENJ[i].activos=0; ENJ[i].detonaciones=0; ENJ[i].camara_reporto=0;
        ENJ[i].last_reassign_ts=0; ENJ[i].last_donor=-1; ENJ[i].last_donor_ts=0;
        pthread_mutex_init(&ENJ[i].lock,NULL);
        BL[i].id=i; BL[i].pos=CFG.objetivos[i]; BL[i].estado=0; BL[i].impactos=0;
    }
    for(int a=0;a<CFG.num_objetivos;a++)
        for(int b=0;b<CFG.num_objetivos;b++)
            last_pair_move[a][b]=0;
}

static void eval_blanco(int ej){
    if(ej<0||ej>=CFG.num_objetivos) return;
    if(ENJ[ej].detonaciones>=4 && ENJ[ej].completos) BL[ej].estado=2; // total
    else if(ENJ[ej].detonaciones>0) BL[ej].estado=1;                 // parcial
}

static void maybe_mark_enjambre_completo(int ej){
    if(ej<0||ej>=CFG.num_objetivos) return;
    if(!ENJ[ej].completos && ENJ[ej].ens_attack>=4 && ENJ[ej].ens_camera>=1){
        ENJ[ej].completos=1;
        // notifica a todos los drones de ese enjambre
        for(int i=0;i<MAX_DRONES;i++){
            if(DR[i].id==i && DR[i].enjambre_id==ej && DR[i].conectado && !DR[i].finalizado){
                send_to_drone(DR[i].sock,"ENJAMBRE_COMPLETO");
            }
        }
        printf("[CMD] Enjambre obj %d COMPLETO\n", ej);
    }
}

// ------------- re-ensamblaje -------------
static int donor_has_type(int ej, int tipo){
    // no robar de completos
    if(ENJ[ej].completos) return 0;
    // tiene alguien activo?
    if(ENJ[ej].activos<=1) return 0;
    // tiene del tipo pedido?
    if(tipo==0) return ENJ[ej].ens_attack>0;
    else        return ENJ[ej].ens_camera>0;
}

static int pick_donor_drone(int donor, int tipo){
    time_t now = time(NULL);
    for(int i=0;i<MAX_DRONES;i++){
        if(DR[i].id==i && DR[i].enjambre_id==donor && DR[i].conectado && !DR[i].finalizado && DR[i].tipo==tipo){
            if(DR[i].en_ensamble!=1) continue;
            if(DR[i].last_moved_ts && (now - DR[i].last_moved_ts) < 5) continue;
            return i;
        }
    }
    return -1;
}

static int needed_type_for(int dest){
    // Prioridad: falta cámara -> 1; si no, falta ataque -> 0; si nada falta -> -1
    if(ENJ[dest].ens_camera<1) return 1;
    if(ENJ[dest].ens_attack<4) return 0;
    return -1;
}

static int try_reassign_one(int dest){
    time_t now=time(NULL);
    if(now-ENJ[dest].last_reassign_ts < REASSIGN_COOLDOWN) return 0;

    int needed = needed_type_for(dest);
    if(needed==-1) return 0; // ya no falta nada (o está completo)

    for(int r=1; r<CFG.num_objetivos; r++){
        int cand[2]={dest-r, dest+r};
        for(int k=0;k<2;k++){
            int ej=cand[k]; if(ej<0||ej>=CFG.num_objetivos||ej==dest) continue;

            if(pthread_mutex_trylock(&ENJ[ej].lock)==0){
                if(pthread_mutex_trylock(&ENJ[dest].lock)==0){
                    int pair_ok = (now - last_pair_move[ej][dest] >= PAIR_COOLDOWN);
                    if(pair_ok && donor_has_type(ej, needed)){
                        int did = pick_donor_drone(ej, needed);
                        if(did>=0){
                            // mover
                            char reas[40]; snprintf(reas,sizeof(reas),"REASIGNAR:%d",dest); send_to_drone(DR[did].sock, reas);
                            char dsp[32];  snprintf(dsp ,sizeof(dsp ),"DESPEGAR:%d",dest); send_to_drone(DR[did].sock, dsp);

                            ENJ[ej].activos--; ENJ[dest].activos++;
                            if(needed==0){ ENJ[dest].ens_attack++; ENJ[ej].ens_attack--; }
                            else         { ENJ[dest].ens_camera++; ENJ[ej].ens_camera--; }

                            DR[did].enjambre_id = dest;
                            DR[did].last_moved_ts = now;
                            ENJ[dest].last_reassign_ts=now; ENJ[dest].last_donor=ej; ENJ[dest].last_donor_ts=now;
                            last_pair_move[ej][dest]=now;

                            if(CFG.verbose){
                                printf("[REASM] obj %d ← dron %d (tipo=%s) desde obj %d\n",
                                       dest, DR[did].id, needed==0?"ATAQUE":"CAMARA", ej);
                            }

                            pthread_mutex_unlock(&ENJ[dest].lock); pthread_mutex_unlock(&ENJ[ej].lock);
                            maybe_mark_enjambre_completo(dest);
                            return 1;
                        }
                    }
                    pthread_mutex_unlock(&ENJ[dest].lock);
                }
                pthread_mutex_unlock(&ENJ[ej].lock);
            }
        }
    }
    return 0;
}

static void reensamblar(void){
    for(int i=0;i<CFG.num_objetivos;i++){
        if(!ENJ[i].completos){
            (void)try_reassign_one(i);
        }
    }
}

// ------------- fin/estadísticas -------------
static int check_termination(void){
    time_t now = time(NULL);
    // Warm-up: no terminar en los primeros 5s
    if ((int)(now - SIM_START) < 5) return 0;

    int atacados=0;
    for(int i=0;i<CFG.num_objetivos;i++) if(BL[i].estado>0) atacados++;

    int registrados=0, finalizados=0, activos=0;
    for(int i=0;i<MAX_DRONES;i++){
        if(DR[i].id!=-1) registrados++;
        if(DR[i].finalizado) finalizados++;
        if(DR[i].conectado && !DR[i].finalizado) activos++;
    }

    // Si aún no hay drones registrados, no termines
    if (registrados == 0) return 0;

    if(CFG.num_objetivos>0 && atacados>=CFG.num_objetivos) return 1;
    if((int)(now - SIM_START) >= SIM_TIMEOUT) return 1;
    if(finalizados >= (registrados*9/10)) return 1;
    if(activos==0) return 1;

    return 0;
}

static void print_stats(void){
    int b_tot=0,b_par=0,b_int=0;
    for(int i=0;i<CFG.num_objetivos;i++){
        if(BL[i].estado==2) b_tot++; else if(BL[i].estado==1) b_par++; else b_int++;
    }
    printf("\n==== RESUMEN ====\n");
    printf("Blancos: Total=%d  Parcial=%d  Intactos=%d  (de %d)\n", b_tot,b_par,b_int, CFG.num_objetivos);
    int exito=0; for(int i=0;i<MAX_DRONES;i++) if(DR[i].id!=-1 && (DR[i].detono||DR[i].reporto||DR[i].finalizado)) exito++;
    printf("Drones finalizados: %d\n", exito);
    float efect = (float)(b_tot+b_par)/(float)(CFG.num_objetivos?CFG.num_objetivos:1)*100.f;
    printf("Efectividad: %.1f%%\n", efect);
}

// ------------- manejo mensajes -------------
static void handle_registration(const char*msg,int sock){
    int id,tipo,ej; if(sscanf(msg,"REGISTRO:%d:%d:%d",&id,&tipo,&ej)!=3) return;
    if(id<0||id>=MAX_DRONES) return;
    if(DR[id].sock>0 && DR[id].sock!=sock) close(DR[id].sock);

    DR[id].id=id; DR[id].tipo=tipo; DR[id].enjambre_id=ej; DR[id].sock=sock;
    DR[id].conectado=1; DR[id].finalizado=0; DR[id].en_ensamble=0; DR[id].detono=0; DR[id].reporto=0; DR[id].derribado=0; DR[id].last_msg=time(NULL); DR[id].last_moved_ts=0;
    ENJ[ej].activos++;

    char cmd[24]; snprintf(cmd,sizeof(cmd),"DESPEGAR:%d",ej); send_to_drone(sock,cmd);
}

static void handle_pos(const char*msg){
    int id,x,y,comb; if(sscanf(msg,"POS:%d:%d_%d:%d",&id,&x,&y,&comb)!=4) return;
    if(id<0||id>=MAX_DRONES||DR[id].id==-1) return;
    DR[id].pos.x=x; DR[id].pos.y=y; DR[id].comb=comb; DR[id].last_msg=time(NULL);
}

static void handle_evt(const char*msg){
    int id; char ev[64]; if(sscanf(msg,"EVT:%d:%63s",&id,ev)!=2) return;
    if(id<0||id>=MAX_DRONES||DR[id].id==-1) return;
    int ej=DR[id].enjambre_id;

    if(!strcmp(ev,"LLEGUE_ENSAMBLE")){
        if(!DR[id].en_ensamble){
            DR[id].en_ensamble=1;
            if(DR[id].tipo==0) ENJ[ej].ens_attack++;
            else               ENJ[ej].ens_camera++;
            maybe_mark_enjambre_completo(ej);
            // si el enjambre ya estaba completo, ordena salir de inmediato
            if(ENJ[ej].completos && DR[id].sock>=0){
                send_to_drone(DR[id].sock, "ENJAMBRE_COMPLETO");
            }
        }
    } else if(!strcmp(ev,"SALIENDO_ENSAMBLE")){
        if(DR[id].en_ensamble){
            DR[id].en_ensamble=0;
            if(DR[id].tipo==0 && ENJ[ej].ens_attack>0) ENJ[ej].ens_attack--;
            if(DR[id].tipo==1 && ENJ[ej].ens_camera>0) ENJ[ej].ens_camera--;
        }
    } else if(!strcmp(ev,"DETONACION")){
        if(DR[id].tipo==0 && !DR[id].detono){
            DR[id].detono=1; ENJ[ej].detonaciones++; BL[ej].impactos++; DR[id].finalizado=1; ENJ[ej].activos--;
            eval_blanco(ej);
            printf("[CMD] Impacto en obj %d (detonaciones=%d)\n", ej, ENJ[ej].detonaciones);
        }
    } else if(!strcmp(ev,"OBJETIVO_REPORTADO")){
        if(DR[id].tipo==1 && !DR[id].reporto){
            DR[id].reporto=1; ENJ[ej].camara_reporto=1; DR[id].finalizado=1; ENJ[ej].activos--;
            printf("[CMD] Reporte cámara obj %d\n", ej);
        }
    } else if(!strcmp(ev,"SIN_COMBUSTIBLE")){
        if(!DR[id].finalizado){ DR[id].finalizado=1; ENJ[ej].activos--; }
    } else if(!strcmp(ev,"COM_PERDIDA")){
        DR[id].conectado=0;
    } else if(!strcmp(ev,"COM_RESTABLECIDA")){
        DR[id].conectado=1; DR[id].last_msg=time(NULL);
    }
}

static void handle_artillery(const char*msg){
    int id; if(sscanf(msg,"EVT_DERRIBO:%d",&id)!=1) return;
    if(id<0||id>=MAX_DRONES||DR[id].id==-1||DR[id].finalizado) return;

    int ej=DR[id].enjambre_id;
    DR[id].derribado=1; DR[id].finalizado=1; ENJ[ej].activos--;

    // Orden explícita de abortar al dron
    if(DR[id].sock>=0){
        send_to_drone(DR[id].sock, "ABORTAR");
    }
    // Si el dron estaba contado en ensamble, ajustar contadores por tipo
    if(DR[id].en_ensamble){
        if(DR[id].tipo==0 && ENJ[ej].ens_attack>0) ENJ[ej].ens_attack--;
        if(DR[id].tipo==1 && ENJ[ej].ens_camera>0) ENJ[ej].ens_camera--;
        ENJ[ej].completos = (ENJ[ej].ens_attack>=4 && ENJ[ej].ens_camera>=1);
    }

    // Intentar re-ensamblar si ese enjambre quedó incompleto
    if(!ENJ[ej].completos) reensamblar();
}

// ------------- bucle principal -------------
int main(void){
    if(!load_config(&CFG)) return 1;
    srv_sock=open_server(CFG.puerto_comando); if(srv_sock<0){ perror("comando:listen"); return 1; }
    SIM_START=time(NULL);
    init_state();

    for(;;){
        fd_set rfds; FD_ZERO(&rfds); FD_SET(srv_sock,&rfds); int maxfd=srv_sock;
        for(int i=0;i<MAX_DRONES;i++) if(DR[i].id!=-1 && DR[i].sock>=0){ FD_SET(DR[i].sock,&rfds); if(DR[i].sock>maxfd) maxfd=DR[i].sock; }
        struct timeval tv={1,0}; int r=select(maxfd+1,&rfds,NULL,NULL,&tv);
        if(r<0){ perror("select"); break; }

        if(FD_ISSET(srv_sock,&rfds)){
            int cs=accept(srv_sock,NULL,NULL);
            if(cs>=0){
                char buf[256]; int n=recv(cs,buf,sizeof(buf)-1,0);
                if(n>0){ buf[n]='\0';
                    if(!strncmp(buf,"REGISTRO:",9)){ handle_registration(buf,cs); cs=-1; }
                    else if(!strncmp(buf,"EVT_DERRIBO:",12)){ handle_artillery(buf); }
                }
                if(cs>=0) close(cs);
            }
        }
        for(int i=0;i<MAX_DRONES;i++){
            if(DR[i].id!=-1 && DR[i].sock>=0 && FD_ISSET(DR[i].sock,&rfds)){
                char buf[256]; int n=recv(DR[i].sock,buf,sizeof(buf)-1,0);
                if(n<=0){ close(DR[i].sock); DR[i].sock=-1; DR[i].conectado=0; }
                else { buf[n]='\0';
                    if(!strncmp(buf,"POS:",4)) handle_pos(buf);
                    else if(!strncmp(buf,"EVT:",4)) handle_evt(buf);
                    else if(!strncmp(buf,"REGISTRO:",9)) handle_registration(buf,DR[i].sock);
                }
            }
        }
        reensamblar();
        if(check_termination()) break;
    }
    print_stats();
    if(srv_sock>=0) close(srv_sock);
    return 0;
}
