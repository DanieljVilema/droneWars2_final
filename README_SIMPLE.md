# 🚁 **DroneWars2 - Simulador de Enjambres de Drones**

## 🚀 **Ejecución Simple**

```bash
# Compilar
make clean && make

# Ejecutar
./main
```

## ⚙️ **Configuración**

Edita `config.txt` para cambiar parámetros:

```bash
# Modo de salida
VERBOSE=0    # 0=silencioso, 1=verbose

# Probabilidades
PROBABILIDAD_DERRIBO=15      # % de derribo por artillería
PROBABILIDAD_PERDIDA_COM=5   # % de pérdida de comunicación
```

## 📊 **Modos de Salida**

### **Modo Silencioso (VERBOSE=0)**
- Solo información esencial
- Pérdidas significativas (cada 5 drones)
- Eventos críticos (ataques, detonaciones)
- Resumen final

### **Modo Verbose (VERBOSE=1)**
- Todos los detalles
- Mensajes de reensamblaje
- Debug completo
- Estado del sistema

## 🔧 **Cambiar Modo**

```bash
# Editar config.txt
nano config.txt

# Cambiar VERBOSE=0 o VERBOSE=1

# Recompilar y ejecutar
make clean && make
./main
```

## 📁 **Estructura del Proyecto**

```
droneWars2_final/
├── src/           # Código fuente
├── config.txt     # Configuración principal
├── Makefile       # Compilación
├── main           # Ejecutable principal
└── README_*.md    # Documentación
```

## 🎯 **Flujo de la Simulación**

1. **Lanzamiento**: Drones salen de camiones con objetivos aleatorios
2. **Ensamblaje**: Forman enjambres de 4A+1C en zonas de ensamble
3. **Ataque**: Viajan coordinadamente hacia objetivos
4. **Combate**: Encuentran defensa enemiga (artillería)
5. **Reensamblaje**: Se reorganizan si hay pérdidas
6. **Finalización**: Ataque final y reporte de estado

## 🚨 **Solución de Problemas**

### **Error de compilación**
```bash
make clean
make
```

### **Permisos de ejecución**
```bash
chmod +x main
```

### **Puerto en uso**
```bash
# Cambiar en config.txt
PUERTO_COMANDO=8081
PUERTO_BASE_ARTILLERIA=9001
```

## 📝 **Logs y Debug**

- **Modo silencioso**: Solo eventos importantes
- **Modo verbose**: Todo el flujo del sistema
- **Logs**: Se muestran en consola durante la ejecución

## 🏁 **Resultados**

Al final de la simulación se muestra:
- Estado de objetivos (intacto/parcial/destruido)
- Efectividad del ataque
- Composición final de enjambres
- Estadísticas de drones
