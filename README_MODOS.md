# 🎯 **MODOS DE SALIDA DE LA SIMULACIÓN**

## 📋 **RESUMEN**

La simulación tiene **dos modos de salida** para controlar qué información se muestra:

- **🔇 MODO SILENCIOSO (VERBOSE=0)**: Solo información esencial y crítica
- **🔊 MODO VERBOSE (VERBOSE=1)**: Todos los detalles y mensajes de debug

---

## 🔇 **MODO SILENCIOSO (VERBOSE=0)**

### **¿Qué se muestra?**
- ✅ **Registro de drones** (cuando se conectan)
- ✅ **Formación de enjambres** (cuando se completan)
- ✅ **Pérdidas significativas** (cada 5 drones perdidos)
- ✅ **Ataques y detonaciones** (eventos críticos)
- ✅ **Reportes de cámara** (estado de objetivos)
- ✅ **Resumen final** (estadísticas completas)

### **¿Qué NO se muestra?**
- ❌ Mensajes repetitivos de "condiciones perfectas"
- ❌ Mensajes repetitivos de "pérdidas detectadas"
- ❌ Detalles de donaciones entre enjambres
- ❌ Debug detallado de cada objetivo
- ❌ Mensajes de comunicación de drones

---

## 🔊 **MODO VERBOSE (VERBOSE=1)**

### **¿Qué se muestra?**
- ✅ **TODO** del modo silencioso
- ✅ **Todos** los mensajes de reensamblaje
- ✅ **Detalles** de donaciones entre enjambres
- ✅ **Debug** completo de cada objetivo
- ✅ **Mensajes** de comunicación de drones
- ✅ **Estado** completo del sistema

---

## 🚀 **CÓMO CAMBIAR ENTRE MODOS**

### **Editar config.txt directamente:**
```bash
# Para modo SILENCIOSO
VERBOSE=0

# Para modo VERBOSE
VERBOSE=1
```

---

## 📊 **COMPARACIÓN DE SALIDAS**

### **MODO SILENCIOSO:**
```
[C0] DRN 0 ATAQUE → OBJ 0
[C0] DRN 1 ATAQUE → OBJ 1
[CMD] Enjambre obj 0 COMPLETO
[SISTEMA] Pérdidas: 27/28 drones - reensamblaje activo
[DRN 202] Detonó
[CMD] Reporte cámara obj 0 - Estado del blanco: PARCIALMENTE DAÑADO (1 detonaciones)
==== RESUMEN ====
Blancos: Total=1  Parcial=0  Intactos=3  (de 4)
```

### **MODO VERBOSE:**
```
[C0] DRN 0 ATAQUE → OBJ 0
[C0] DRN 1 ATAQUE → OBJ 1
[CMD] Enjambre obj 0 COMPLETO
[DONACION] obj 0 → obj 1: dron 106 (CAMARA)
[SISTEMA] Condiciones perfectas - reensamblaje limitado
[SISTEMA] Pérdidas detectadas (27/28 drones) - iniciando reensamblaje
[DRN 202] Detonó
[CMD] Reporte cámara obj 0 - Estado del blanco: PARCIALMENTE DAÑADO (1 detonaciones)
[DEBUG] Obj 0: INCOMPLETO, 1 det, PARCIAL
==== RESUMEN ====
```

---

## 🎯 **RECOMENDACIONES**

### **Usar MODO SILENCIOSO cuando:**
- 🎯 **Desarrollo**: Para probar la lógica principal
- 🎯 **Presentación**: Para mostrar resultados limpios
- 🎯 **Producción**: Para monitoreo en tiempo real
- 🎯 **Debugging**: Para enfocarse en problemas específicos

### **Usar MODO VERBOSE cuando:**
- 🔍 **Debugging detallado**: Para entender el flujo completo
- 🔍 **Desarrollo**: Para ver todos los detalles del sistema
- 🔍 **Análisis**: Para estudiar el comportamiento completo
- 🔍 **Testing**: Para verificar cada paso de la simulación

---

## 🚀 **EJECUCIÓN**

```bash
# Compilar
make clean && make

# Ejecutar (usa la configuración actual)
./main

# Para cambiar modo, editar config.txt y cambiar VERBOSE=0 o VERBOSE=1
```

---

## 💡 **CONSEJOS**

1. **Empieza en modo SILENCIOSO** (VERBOSE=0) para ver el flujo principal
2. **Cambia a VERBOSE** (VERBOSE=1) solo cuando necesites debug detallado
3. **Edita config.txt** directamente para cambiar entre modos
4. **Recompila** después de cambiar la configuración
5. **Mantén VERBOSE=0** como configuración por defecto
