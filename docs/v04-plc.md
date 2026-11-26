# V0.4 — Motor lógico experimental
Base inicial: V0.3.3 validada por el usuario; el sketch V0.4.0 toma el firmware V0.3.5 con pulsos largos. Rama feature/v04-plc; no desplegar todavía.
## Motor
Cabecera C++ autónoma sin GPIO, asignaciones dinámicas ni acceso a red. Máximo 48 bloques; 6 DI crudas, 4 AI de 12 bits, 4 salidas booleanas virtuales. Referencias anteriores exclusivamente (sin ciclos combinacionales), salidas sin asignar apagadas. NOT para contactos activos a nivel bajo. AND/OR/NOT, constantes, comparadores >= y <=, RS con prioridad de reset, TON/TOF/TP (20 ms a 600 s), CTU/CTD (1 a 65535). Contadores saturados, prioridad reset/recarga, flanco ascendente sin pulso artificial en primer scan. TP no redisparable. No hay histéresis todavía.
Instalar programa mantiene STOP; requiere arm explícito. STOP y fallos ponen salidas virtuales a cero, borran estados. Un intervalo de scan superior a 250 ms dispara fallo; objetivo de scan de 20 ms (modo simulación, no tiempo real garantizado). No arranque automático tras reinicio. Temporizadores y conteos no retentivos. No es un PLC de seguridad ni apto para controlar cargas sin protecciones independientes.
## Integración pendiente
1. Modo simulación local implementado; compilación ESP32 y prueba física pendientes.
2. Validación del formato JSON FBD, persistencia NVS separada y transferencia autenticada.
3. Editor gráfico y monitor de bloques en Render.
4. Arbitraje manual/automático, paro prioritario, watchdog independiente, protecciones y pruebas físicas. La pérdida de internet no debe interrumpir la lógica autónoma; política de fallo y arranque explícita. No conectar este motor a los GPIO antes de validar estas condiciones.
## Pruebas
g++ -std=c++17 -Wall -Wextra -Werror -Wno-misleading-indentation -pedantic firmware/plc/tests/test_plc.cpp -o /tmp/plc-test && /tmp/plc-test
Workflow PLC engine tests ejecuta pruebas en GitHub Actions. Esta fase no cambia el firmware activo ni Render. CI compila también el sketch ESP32 con el core 3.3.12 y ArduinoJson 7.4.2.

## Simulación local V0.4.0
- Descargar los dos archivos de firmware/Yarumo_X4_V04 en una carpeta llamada Yarumo_X4_V04. Conservar las mismas particiones OTA y la NVS; no borrar la flash. Si ya se guardaron las credenciales, no copiar secrets.h; no se necesitan claves nuevas. ArduinoJson 7.
- No cargar hasta que la compilación sea verificada. Probar sin cargas eléctricas.
- Acceder al panel local autenticado y seleccionar "Abrir simulador PLC V0.4" (/plc). Iniciar uno de los tres programas: TON (DI1 virtual, 2 s), RS (DI1 marcha y DI2 reset), CTU (3 flancos ascendentes DI1, DI2 reset). Cambiar DI1 y DI2 con los botones. DO1-DO4 son indicadores virtuales; no se asignan a GPIO.
- Al iniciar se apagan los relés reales y se bloquea toda orden de activación física, incluso desde Render y serial. STOP apaga las salidas virtuales; el bloqueo físico permanece hasta reinicio. OTA detiene la simulación. No hay arranque automático del programa.
- No se cambia el servidor. Para regresar a V0.3.5, cargar el sketch anterior por OTA con NVS preservada. Validar persistencia y reconexión antes de cualquier prueba adicional.
- El motor fue aprobado en pruebas nativas de la fase 1. La compilación del sketch integrado y las pruebas físicas todavía deben comprobarse; no es una función PLC de producción.
