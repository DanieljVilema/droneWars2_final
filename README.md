# droneWars2_final

## Compilación

```
make
```

Modos:

- `make debug` para compilación con warnings como errores y símbolos de depuración.
- `make asan` para Address/UBSan.
- `make tsan` para ThreadSanitizer.
- `make test` para ejecutar pruebas unitarias e integración básica.

Ejecutar simulación con escenario:

```
make run-sim SCENARIO=scenarios/basic.cfg
```