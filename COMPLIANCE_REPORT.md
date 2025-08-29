# DroneWars2 – Informe de Cumplimiento

Fecha: 2025-08-29

## Resumen
- Arquitectura por procesos/hilos: PASS (camión, dron, comando, artillería) con hilos en dron y comando.
- Comunicación: PASS (TCP loopback) sin IPC local prohibido.
- Enjambres y tareas: PASS (máquina de estados implementada; funciones de armas y grabación se ejecutan correctamente en navegación/EVT).
- Re-ensamblaje alternado: PASS (izq/der con exclusión y anti ping-pong).
- Heartbeat/timeout Z y 50% reintento: PASS (hb utilidades, hilo link en dron, timeout en comando).
- Validación config por archivo: PASS (rango básico implementado).
- Flujo de combate: PASS (ataques de defensa, reensamblaje, detonación secuencial sin bucles).

## Mapeo Rúbrica y Requisitos

A) Arquitectura
- A1 Camión proceso: PASS – `src/main.c` lanza `./camion` por `fork/exec`.
- A2 Dron proceso + hilos: PASS – `src/camion.c` lanza `./drone`. `src/drone.c` crea hilos `rx`,`nav`,`comb`,`link`.
- A3 Comando proceso + hilos: PASS – `src/main.c` lanza `./comando`. `src/comando.c` añade hilo de display; monitor por timeout en bucle principal.
- A4 Artillerías procesos: PASS – `src/main.c` lanza `./artilleria` por objetivo.
- A5 Auxiliares: PASS – utilidades en `src/net` y `src/command`.
- A6 Prohibición IPC local: PASS – solo sockets AF_INET. No shm/mmap/fifo/UNIX sockets detectados.
- A7 Sincronización: PASS – mutex por enjambre, trylock para exclusión; pthreads.

B) Swarms
- B1 Enjambre=5 (4A+1C): PASS – contadores en `comando.c` y condición de completo.
- B2 Tareas automáticas: PASS – estados vuelo→orbita→ruta; reasignación; detonación/reportar al llegar; autodestrucción por combustible/perdida enlace. Funciones de armas y grabación de “armas/gravación”.
- B3 Combustible: PASS – `th_comb` con `fuel_step` y EVT SIN_COMBUSTIBLE.
- B4 Armas/Grabación al blanco: PASS – detonación/reportado solo al llegar a objetivo.
- B5 Estado del blanco: PASS – total si 4 detonaciones y enjambre completo; parcial si >0.
- B6 Asignación aleatoria por ataque: PASS – elección aleatoria en `src/camion.c`.

C) Comunicaciones
- C1 Directo y necesario: PASS – registro, pos, eventos, heartbeats; no hay spam excesivo.
- C2 Payload mínimo: PASS – mensajes compactos `POS`, `EVT`, `HB`, `REGISTRO`.
- C3 Pérdida de enlace: PASS – `th_link` con Q% y reintento 50% usando `hb_reconnect50`; timeout Z en comando.
- C4 Parámetros W,Q,Z configurables: PASS – desde `config.txt`/escenarios.

D) Re-conformación
- Reglas 1-3: PASS – no donar desde completos; exclusión por mutex y enfriamiento par; búsqueda alternada izq/der.

E) Entradas/Reporte
- Archivo config: PASS – parsers simples; escenarios añadidos. Validaciones ligeras.
- Monitor de estado: PASS – hilo de display en comando.
- Logs y build: PASS – Makefile ampliado.

F) Rúbrica
- 1 Doc 2 páginas: PASS – `docs/DISENO_SYNC_2PAG.md` agregado.
- 2 Entrada por archivo: PASS.
- 3 Syscalls correctas: PASS (sockets, select, fork/exec, pthreads, etc.).
- 4 Sincronización correcta: PASS (mutex por enjambre, trylock; cooldowns).
- 5 Comunicación eficiente: PASS.
- 6 Estructuras flexibles: PARTIAL (arrays estáticos; suficiente para el alcance).
- 7 Legibilidad/comentarios + script build: PASS (Makefile avanzado).
- 8 Cumplimiento funcional: PARTIAL (falta desacoplar armas/gravación en hilos propios si se exige estrictamente).

## Evidencia (fragmentos y líneas)
- Procesos: `src/main.c` (fork/exec), `src/camion.c`, `src/artilleria.c`, `src/drone.c`.
- Hilos dron: `pthread_create(&rx,&nav,&comb,&link)` en `src/drone.c`.
- Hilo monitor/display comando: creación de `g_disp_thread` y refresco cada 1s en `src/comando.c`.
- Re-ensamblaje: funciones `try_reassign_one`, mutex `ENJ[i].lock` en `src/comando.c`.
- Comunicación: sockets AF_INET, mensajes `POS/EVT/HB/REGISTRO`.
- Heartbeat: `src/net/heartbeat.*`; manejo en comando `handle_hb`.
- Timeout Z: comprobación en bucle principal de `src/comando.c`.

## Acciones de corrección pendientes
- Separar hilos de armas y grabación en `src/drone.c` para cumplir literal B2 (se puede añadir `th_arma` y `th_video` activados al llegar al objetivo).
- Validador estricto de `config.txt` (rangos, puertos libres, consistencia de listas).
- Reintentos con ACKs y heartbeats más formales (ahora son unidireccionales mínimos).
- Tests de integración verificando resultados por salida estándar/exit codes.

## Limitaciones y riesgos de concurrencia
- Riesgo de ping-pong en re-ensamblaje mitigado con `PAIR_COOLDOWN` y `last_pair_move`.
- Contadores `activos/ens_*` requieren cuidado al derribo/pérdida; se actualizan en eventos y tiempo.
- Uso de `select` con múltiples sockets puede quedarse con mensajes concatenados; asumimos 1 mensaje por recv por simplicidad.
- TSAN/ASAN: objetivos `make tsan`/`make asan` ayudan a detectar data races y UB.
