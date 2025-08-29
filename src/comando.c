/*
 * Centro de Comando - comando.c
 * Curso: Sistemas Operativos
 * 
 * Este programa coordina todos los drones del sistema:
 * - Forma enjambres de 5 drones (4 ataque + 1 camara) por objetivo
 * - Re-asigna drones entre enjambres cuando es necesario
 * - Maneja comunicacion con drones y artilleria
 * - Muestra progreso de la mision
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
    int en_mision;             // 1 si ya salió hacia el objetivo
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
        ENJ[i].en_mision=0; ENJ[i].activos=0; ENJ[i].detonaciones=0; ENJ[i].camara_reporto=0;
        ENJ[i].last_reassign_ts=0; ENJ[i].last_donor=-1; ENJ[i].last_donor_ts=0;
        pthread_mutex_init(&ENJ[i].lock,NULL);
        BL[i].id=i; BL[i].pos=CFG.objetivos[i]; BL[i].estado=0; BL[i].impactos=0;
    }
    for(int a=0;a<CFG.num_objetivos;a++)
        for(int b=0;b<CFG.num_objetivos;b++)
            last_pair_move[a][b]=0;
}

static void print_final_swarm_composition() {
    printf("\n==== COMPOSICIÓN FINAL DE ENJAMBRES ====\n");
    for(int i = 0; i < CFG.num_objetivos; i++) {
        int attack_count = 0, camera_count = 0;
        int attack_survived = 0, camera_survived = 0;
        
        // Contar drones por tipo que participaron en este enjambre
        for(int j = 0; j < MAX_DRONES; j++) {
            if(DR[j].id == j && DR[j].enjambre_id == i) {
                if(DR[j].tipo == 0) { // Ataque
                    attack_count++;
                    if(!DR[j].finalizado) attack_survived++;
                } else { // Cámara
                    camera_count++;
                    if(!DR[j].finalizado) camera_survived++;
                }
            }
        }
        
        printf("[ENJAMBRE %d] Asignados: %dA+%dC | Sobrevivientes: %dA+%dC | Estado: %s | Detonaciones: %d\n",
               i, attack_count, camera_count, attack_survived, camera_survived,
               ENJ[i].completos ? "COMPLETO" : "INCOMPLETO", ENJ[i].detonaciones);
    }
    printf("===============================================\n");
}

static void eval_blanco(int ej){
    if(ej<0||ej>=CFG.num_objetivos) return;
    if(ENJ[ej].detonaciones>=4) BL[ej].estado=2;        // total: 4+ detonaciones
    else if(ENJ[ej].detonaciones>0) BL[ej].estado=1;    // parcial: 1-3 detonaciones
    // intacto (estado=0) si detonaciones==0
}

// Función para donar excedentes cuando un enjambre esté completo
static void donate_excess_drones(int ej){
    if(ej<0||ej>=CFG.num_objetivos) return;
    
    // NO donar si el enjambre ya está en misión (evitar interferir con ataques)
    if(ENJ[ej].en_mision) return;
    
    time_t now = time(NULL);
    
    // Verificar si estamos en condiciones perfectas (sin pérdidas)
    int condiciones_perfectas = 1;
    int total_drones_activos = 0;
    int total_drones_esperados = 0;
    
    for(int i=0; i<CFG.num_objetivos; i++){
        total_drones_activos += ENJ[i].activos;
        total_drones_esperados += 5; // 5 drones por objetivo esperados (4A+1C)
    }
    
    // Si tenemos menos drones activos de los esperados, no estamos en condiciones perfectas
    if(total_drones_activos < (CFG.num_objetivos * 4)) { // Al menos 4 por objetivo
        condiciones_perfectas = 0;
    }
    
    // En condiciones perfectas, ser más agresivo con las donaciones
    int puede_donar = 0;
    if(condiciones_perfectas){
        // En condiciones perfectas, donar si tiene excedentes (aún sin estar completo)
        puede_donar = (ENJ[ej].ens_attack > 4 || ENJ[ej].ens_camera > 1);
    } else {
        // En condiciones normales, solo donar si ya está completo
        puede_donar = ENJ[ej].completos;
    }
    
    if(!puede_donar) return;
    
    // Donar drones de ataque excedentes (más de 4)
    while(ENJ[ej].ens_attack > 4){
        // En condiciones perfectas, donar agresivamente
        // En condiciones normales, mantener al menos 4 si está completo
        if(!condiciones_perfectas && ENJ[ej].completos && ENJ[ej].ens_attack <= 4) break;
        
        // Buscar un enjambre que necesite drones de ataque
        int best_dest = -1;
        for(int dest=0; dest<CFG.num_objetivos; dest++){
            if(dest == ej) continue; // No donarse a sí mismo
            if(ENJ[dest].en_mision) continue; // No donar a enjambres en misión
            if(!condiciones_perfectas && ENJ[dest].completos) continue; // En condiciones normales, no donar a completos
            if(ENJ[dest].ens_attack < 4){ // Necesita drones de ataque
                best_dest = dest;
                break;
            }
        }
        
        if(best_dest == -1) break; // No hay quien necesite
        
        // Buscar un dron de ataque disponible para donar
        int donor_drone = -1;
        for(int i=0; i<MAX_DRONES; i++){
            if(DR[i].id==i && DR[i].enjambre_id==ej && DR[i].conectado && 
               !DR[i].finalizado && DR[i].tipo==0 && DR[i].en_ensamble==1){
                if(DR[i].last_moved_ts && (now - DR[i].last_moved_ts) < 3) continue; // Reducido a 3s en condiciones perfectas
                donor_drone = i;
                break;
            }
        }
        
        if(donor_drone == -1) break; // No hay dron disponible
        
        // Realizar la donación
        if(pthread_mutex_trylock(&ENJ[best_dest].lock)==0){
            char reas[40]; snprintf(reas,sizeof(reas),"REASIGNAR:%d",best_dest); 
            send_to_drone(DR[donor_drone].sock, reas);
            char dsp[32]; snprintf(dsp,sizeof(dsp),"DESPEGAR:%d",best_dest); 
            send_to_drone(DR[donor_drone].sock, dsp);

            ENJ[ej].activos--; ENJ[best_dest].activos++;
            ENJ[ej].ens_attack--; ENJ[best_dest].ens_attack++;
            
            DR[donor_drone].enjambre_id = best_dest;
            DR[donor_drone].last_moved_ts = now;
            
            if(CFG.verbose){
                printf("[DONACION%s] obj %d → obj %d: dron %d (tipo=ATAQUE) [balanceando]\n",
                       condiciones_perfectas ? " PERFECTA" : "", ej, best_dest, DR[donor_drone].id);
            }
            
            pthread_mutex_unlock(&ENJ[best_dest].lock);
        } else {
            break; // No se pudo obtener lock
        }
    }
    
    // Donar drones de cámara excedentes (más de 1)
    while(ENJ[ej].ens_camera > 1){
        // En condiciones perfectas, donar agresivamente
        // En condiciones normales, mantener al menos 1 si está completo
        if(!condiciones_perfectas && ENJ[ej].completos && ENJ[ej].ens_camera <= 1) break;
        
        // Buscar un enjambre que necesite drones de cámara
        int best_dest = -1;
        for(int dest=0; dest<CFG.num_objetivos; dest++){
            if(dest == ej) continue;
            if(ENJ[dest].en_mision) continue; // No donar a enjambres en misión
            if(!condiciones_perfectas && ENJ[dest].completos) continue; // En condiciones normales, no donar a completos
            if(ENJ[dest].ens_camera < 1){ // Necesita cámara
                best_dest = dest;
                break;
            }
        }
        
        if(best_dest == -1) break;
        
        // Buscar un dron de cámara disponible para donar
        int donor_drone = -1;
        for(int i=0; i<MAX_DRONES; i++){
            if(DR[i].id==i && DR[i].enjambre_id==ej && DR[i].conectado && 
               !DR[i].finalizado && DR[i].tipo==1 && DR[i].en_ensamble==1){
                if(DR[i].last_moved_ts && (now - DR[i].last_moved_ts) < 3) continue; // Reducido a 3s en condiciones perfectas
                donor_drone = i;
                break;
            }
        }
        
        if(donor_drone == -1) break;
        
        // Realizar la donación
        if(pthread_mutex_trylock(&ENJ[best_dest].lock)==0){
            char reas[40]; snprintf(reas,sizeof(reas),"REASIGNAR:%d",best_dest); 
            send_to_drone(DR[donor_drone].sock, reas);
            char dsp[32]; snprintf(dsp,sizeof(dsp),"DESPEGAR:%d",best_dest); 
            send_to_drone(DR[donor_drone].sock, dsp);

            ENJ[ej].activos--; ENJ[best_dest].activos++;
            ENJ[ej].ens_camera--; ENJ[best_dest].ens_camera++;
            
            DR[donor_drone].enjambre_id = best_dest;
            DR[donor_drone].last_moved_ts = now;
            
            if(CFG.verbose){
                printf("[DONACION%s] obj %d → obj %d: dron %d (tipo=CAMARA) [balanceando]\n",
                       condiciones_perfectas ? " PERFECTA" : "", ej, best_dest, DR[donor_drone].id);
            }
            
            pthread_mutex_unlock(&ENJ[best_dest].lock);
        } else {
            break;
        }
    }
}

static void maybe_mark_enjambre_completo(int ej){
    if(ej<0||ej>=CFG.num_objetivos) return;
    
    // Siempre re-evaluar el estado completo basado en conteos actuales
    int era_completo = ENJ[ej].completos;
    ENJ[ej].completos = (ENJ[ej].ens_attack>=4 && ENJ[ej].ens_camera>=1);
    
    // Si ahora es completo pero antes no era, notificar a los drones
    if(ENJ[ej].completos && !era_completo){
        // notifica a todos los drones de ese enjambre
        for(int i=0;i<MAX_DRONES;i++){
            if(DR[i].id==i && DR[i].enjambre_id==ej && DR[i].conectado && !DR[i].finalizado){
                send_to_drone(DR[i].sock,"ENJAMBRE_COMPLETO");
            }
        }
        printf("[CMD] Enjambre obj %d COMPLETO\n", ej);
        
        // Donar excedentes después de completar (reactivado)
        donate_excess_drones(ej);
    }
}

// ------------- re-ensamblaje -------------
static int donor_has_type(int ej, int tipo){
    // Si está completo, puede donar excedentes
    if(ENJ[ej].completos) {
        if(tipo==0) return ENJ[ej].ens_attack > 4; // Puede donar ataque si tiene más de 4
        else        return ENJ[ej].ens_camera > 1; // Puede donar cámara si tiene más de 1
    }
    
    // Si no está completo, comportamiento original
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
    // Solo reensamblar si hay evidencia de pérdidas en combate
    int hay_perdidas = 0;
    int total_activos = 0;
    int total_inicial = 0;
    
    for(int i=0; i<CFG.num_objetivos; i++){
        total_activos += ENJ[i].activos;
    }
    
    // Calcular total inicial esperado (20A + 8C = 28 drones)
    total_inicial = CFG.num_camiones * 7; // Asumiendo 5A+2C por camión
    
    // Si perdimos drones, entonces hay necesidad de reensamblaje
    if(total_activos < total_inicial) {
        hay_perdidas = 1;
    }
    
    // En condiciones perfectas (sin pérdidas), no hacer reensamblaje agresivo
    if(!hay_perdidas) {
        printf("[SISTEMA] Condiciones perfectas detectadas - reensamblaje limitado\n");
        return;
    }
    
    printf("[SISTEMA] Pérdidas detectadas (%d/%d drones) - iniciando reensamblaje\n", 
           total_activos, total_inicial);
    
    // Primero, intentar reasignar a enjambres incompletos
    for(int i=0;i<CFG.num_objetivos;i++){
        if(!ENJ[i].completos){
            (void)try_reassign_one(i);
        }
    }
    
    // Segundo, donar excedentes de enjambres completos
    for(int i=0;i<CFG.num_objetivos;i++){
        if(ENJ[i].completos){
            donate_excess_drones(i);
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

    // NUEVA LÓGICA: Verificar si todos los enjambres completos han alcanzado su máximo potencial
    int puede_mejorar = 0;
    int tiempo_transcurrido = (int)(now - SIM_START);
    
    for(int i = 0; i < CFG.num_objetivos; i++){
        if(ENJ[i].completos){
            // Si es completo pero aún no ha llegado a 4 detonaciones
            if(ENJ[i].detonaciones < 4){
                // Verificar si aún hay drones activos de ataque que puedan detonar
                int ataques_activos = 0;
                for(int j = 0; j < MAX_DRONES; j++){
                    if(DR[j].id == j && DR[j].enjambre_id == i && 
                       DR[j].conectado && !DR[j].finalizado && 
                       DR[j].tipo == 0 && !DR[j].detono){
                        ataques_activos++;
                    }
                }
                // Si aún hay drones de ataque activos Y no ha pasado mucho tiempo, puede mejorar
                if(ataques_activos > 0 && tiempo_transcurrido < 20){
                    puede_mejorar = 1;
                    break;
                }
            }
        }
    }
    
    // Solo terminar por "todos atacados" si no puede mejorar más
    if(CFG.num_objetivos>0 && atacados>=CFG.num_objetivos && !puede_mejorar) return 1;
    
    // Otras condiciones de terminación
    if((int)(now - SIM_START) >= SIM_TIMEOUT) return 1;
    if(finalizados >= (registrados*9/10)) return 1;
    if(activos==0) return 1;

    return 0;
}

static void print_stats(void){
    int b_tot=0,b_par=0,b_int=0;
    for(int i=0;i<CFG.num_objetivos;i++){
        if(BL[i].estado==0) b_int++;
        else if(BL[i].estado==1) b_par++;
        else b_tot++;
    }

    printf("\n==== RESUMEN ====\n");
    printf("Blancos: Total=%d  Parcial=%d  Intactos=%d  (de %d)\n", b_tot,b_par,b_int,CFG.num_objetivos);
    
    // NUEVO: Debug detallado de cada objetivo
    for(int i=0;i<CFG.num_objetivos;i++){
        printf("[DEBUG] Obj %d: completo=%s, detonaciones=%d, estado=%s\n", 
               i, 
               ENJ[i].completos ? "SI" : "NO", 
               ENJ[i].detonaciones,
               BL[i].estado==0 ? "INTACTO" : (BL[i].estado==1 ? "PARCIAL" : "TOTAL"));
    }

    int exito=0;
    for(int i=0;i<MAX_DRONES;i++) if(DR[i].finalizado) exito++;
    printf("Drones finalizados: %d\n", exito);

    float efectividad = CFG.num_objetivos>0 ? (100.0f*(b_tot+b_par)/CFG.num_objetivos) : 0.0f;
    printf("Efectividad: %.1f%%\n", efectividad);
    
    // Mostrar composición final de enjambres después del reensamblado
    print_final_swarm_composition();
    
    printf("Simulación finalizada.\n");
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
    }

    // Re-evaluar estado completo después del derribo
    maybe_mark_enjambre_completo(ej);

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
        
        // Solo reensamblar si hay enjambres incompletos que necesitan ayuda
        int necesita_reensamblado = 0;
        for(int i=0; i<CFG.num_objetivos; i++){
            if(!ENJ[i].completos && ENJ[i].activos > 0 && !ENJ[i].en_mision) {
                necesita_reensamblado = 1;
                break;
            }
        }
        if(necesita_reensamblado) {
            reensamblar();
        }
        
        if(check_termination()) break;
    }
    print_stats();
    if(srv_sock>=0) close(srv_sock);
    return 0;
}
