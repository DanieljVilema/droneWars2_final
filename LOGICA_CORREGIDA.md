# Lógica Corregida del Sistema DroneWars2

## Flujo Corregido del Sistema

### 1. **Lanzamiento de Drones**
- Los camiones lanzan drones con objetivos asignados aleatoriamente
- Cada camión tiene una carga específica (5A+2C por defecto)
- Los drones se registran en el centro de comando con su tipo y objetivo asignado

### 2. **Formación de Enjambres**
- Los drones viajan a sus zonas de ensamble asignadas
- Se forman enjambres de 4 drones de ataque + 1 drone de cámara
- Si no es posible completar un enjambre, se queda incompleto (mínimo 3A+1C)
- Los drones vuelan en círculos (órbita) mientras esperan

### 3. **Coordinación de Ataques**
- **NUEVO**: El sistema espera a que todos los enjambres tengan al menos 3A+1C
- Solo cuando todos están listos, se inicia la misión simultáneamente
- Se envía comando "INICIAR_MISION" a todos los drones
- Los drones salen de la zona de ensamble hacia sus objetivos

### 4. **Reensamblaje Inteligente**
- Solo se reensambla si hay pérdidas reales (drones derribados o perdidos)
- No se reensambla enjambres que ya están en misión
- Se donan excedentes de enjambres completos a incompletos
- Después del reensamblaje, se verifica si todos están listos para la misión

### 5. **Ejecución de Ataques**
- Los drones viajan hacia sus objetivos
- En el camino pueden ser derribados por la artillería anti-aérea
- La probabilidad de derribo es configurable (15% por defecto)
- Los drones de ataque detonan al llegar al objetivo
- Los drones de cámara se autodestruyen y reportan

### 6. **Evaluación de Objetivos**
- Se requieren 4 detonaciones para destruir completamente un objetivo
- Con menos de 4 detonaciones, el objetivo queda parcialmente dañado
- El sistema termina cuando todos los enjambres completan su misión

## Cambios Implementados

### En `comando.c`:
- Nueva función `all_swarms_ready_for_mission()`: verifica si todos los enjambres están listos
- Nueva función `start_mission_for_all_swarms()`: inicia la misión de todos los enjambres
- Lógica mejorada de terminación: espera a que todos los enjambres completen su misión
- Reensamblaje más inteligente: no interfiere con enjambres en misión

### En `drone.c`:
- Nuevo comando "INICIAR_MISION": permite al centro de comando controlar cuándo salen los drones
- Los drones pueden salir de la zona de ensamble por orden del comando

### En `config.txt`:
- Probabilidad de derribo aumentada a 15% (más realista)
- Probabilidad de pérdida de comunicación a 5%
- Radio de detección de artillería aumentado a 30

## Ventajas de la Nueva Lógica

1. **Sincronización**: Todos los enjambres atacan simultáneamente
2. **Eficiencia**: No hay drones esperando indefinidamente
3. **Realismo**: Las probabilidades de derribo y pérdida de comunicación son realistas
4. **Estabilidad**: El reensamblaje no interfiere con misiones en curso
5. **Coordinación**: El centro de comando tiene control total sobre el timing de los ataques

## Cómo Probar

1. Compilar el proyecto: `make`
2. Ejecutar la simulación: `./main`
3. Observar que:
   - Los drones se forman en enjambres
   - Todos los enjambres esperan a estar listos
   - Los ataques se ejecutan simultáneamente
   - El sistema termina correctamente después de todos los ataques

## Configuración Recomendada

Para diferentes escenarios, ajustar:
- `PROBABILIDAD_DERRIBO`: 0-50% (0% = sin defensa, 50% = defensa muy fuerte)
- `PROBABILIDAD_PERDIDA_COM`: 0-20% (0% = comunicación perfecta, 20% = muy inestable)
- `RADIO_DETECCION_ARTILLERIA`: 20-50 (20 = defensa local, 50 = defensa amplia)
