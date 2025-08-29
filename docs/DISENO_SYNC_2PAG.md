# DroneWars2 – Diseño y Sincronización (2 páginas aprox.)

## Despliegue y procesos/hilos

```mermaid
flowchart LR
    subgraph Camiones [Camiones (procesos)]
        C1[camion] --> D1[drone procesos]
        C2[camion] --> D2[drone procesos]
    end
    subgraph Comando [Centro de comando (proceso)]
        TH1[Hilo sockets]
        TH2[Hilo display]
    end
    subgraph Art [Artillerías (procesos)]
        A1[artilleria #0]
        A2[artilleria #1]
    end

    D1 -- TCP --> Comando
    D2 -- TCP --> Comando
    D1 -- TCP --> A1
    D2 -- TCP --> A2
```

- Camión: proceso que lanza N drones (fork/exec).
- Dron: proceso con hilos rx, navegación, combustible, link (pérdida/reconexión 50%).
- Comando: proceso con bucle select + hilo de display; mutex por enjambre; re-ensamblaje.
- Artillería: proceso/objetivo que derriba con W% en radio.

## Máquina de estados del dron
- INACTIVO → DESPEGADO: al recibir `DESPEGAR:<enj>`.
- DESPEGADO → EN_ORBITA: al llegar a `ZONAS_ENSAMBLE[enj]` y emite `EVT:LLEGUE_ENSAMBLE`.
- EN_ORBITA → EN_RUTA: cuando el enjambre está completo o recibe `ENJAMBRE_COMPLETO`; emite `EVT:SALIENDO_ENSAMBLE`.
- EN_RUTA → FINALIZADO: al llegar al objetivo: `EVT:DETONACION` (ataque) o `EVT:OBJETIVO_REPORTADO` (cámara).
- Cualquier estado → FINALIZADO: SIN_COMBUSTIBLE, ABORTAR, timeout de enlace.

Combustible: `fuel_step()` decrementa al moverse; 0 ⇒ autodestrucción.

## Protocolo de mensajes (compacto)
- Registro: `REGISTRO:<id>:<tipo>:<enj>`
- Telemetría: `POS:<id>:<x>_<y>:<comb>` (rate ~10Hz)
- Heartbeat: `HB:<id>:<ms>` (junto a POS)
- Eventos: `EVT:<id>:<nombre>` (LLEGUE_ENSAMBLE, SALIENDO_ENSAMBLE, DETONACION, OBJETIVO_REPORTADO, SIN_COMBUSTIBLE, COM_PERDIDA, COM_RESTABLECIDA)
- Órdenes: `DESPEGAR:<enj>`, `ENJAMBRE_COMPLETO`, `REASIGNAR:<enj>`, `ABORTAR`
- Artillería→Comando: `EVT_DERRIBO:<id>`

## Re-ensamblaje alternado (izq/der)
- Búsqueda: `dest-1, dest+1, dest-2, dest+2, ...` evitando robar de completos y tipo prioritario (primero cámara, luego ataque).
- Exclusión: `pthread_mutex_trylock(&ENJ[donor].lock)` y `&ENJ[dest].lock` + `PAIR_COOLDOWN` por par.
- Reglas: no extraer de completos; reasignaciones concurrentes por enjambre incompleto.

## Sincronización clave
- Dron: mutex `DR.m` protege estado, combustible, banderas.
- Comando: mutex por enjambre para contadores y reasignación.
- Comunicación: select() multiplexa sockets; heartbeats para frescura.

## Parámetros y validación
- Archivo `config.txt`/`scenarios/*.cfg` define: camiones, objetivos, W (derribo), Q (pérdida), Z (timeout), fuel, puertos.
- Validaciones básicas: formatos, rangos de índices; se pueden reforzar con verificador adicional.

## Consideraciones de rendimiento
- Mensajes compactos, sin JSON; no hay spam de display.
- Estructuras estáticas eficientes; sin asignaciones por mensaje.

## Posibles mejoras
- Hilos separados de armas/cámara en dron para cumplir literalmente el desglose B2.
- ACKs y latidos bidireccionales; serialización binaria.
- Tests de integración con orquestación y verificación de resultados.
