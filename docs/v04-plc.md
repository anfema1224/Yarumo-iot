# V0.4 — Motor lógico experimental
Base: V0.3.3 validada por el usuario en pruebas básicas. Rama feature/v04-plc; no desplegar todavía.
## Motor
Cabecera C++ autónoma sin GPIO, asignaciones dinámicas ni acceso a red. Máximo 48 bloques; 6 DI crudas, 4 AI de 12 bits, 4 salidas booleanas virtuales. Referencias anteriores exclusivamente (sin ciclos combinacionales), salidas sin asignar apagadas. NOT para contactos activos a nivel bajo. AND/OR/NOT, constantes, comparadores >= y <=, RS con prioridad de reset, TON/TOF/TP (20 ms a 600 s), CTU/CTD (1 a 65535). Contadores saturados, prioridad reset/recarga, flanco ascendente sin pulso artificial en primer scan. TP no redisparable. No hay histéresis todavía.
Instalar programa mantiene STOP; requiere arm explícito. STOP y fallos ponen salidas virtuales a cero, borran estados. Un intervalo de scan superior a 250 ms dispara fallo; objetivo de scan de 20 ms (integración pendiente). No arranque automático tras reinicio. Temporizadores y conteos no retentivos. No es un PLC de seguridad ni apto para controlar cargas sin protecciones independientes.
## Integración pendiente
1. Compilación ESP32 y modo simulación local, sin accionar relés.
2. Validación del formato JSON FBD, persistencia NVS separada y transferencia autenticada.
3. Editor gráfico y monitor de bloques en Render.
4. Arbitraje manual/automático, paro prioritario, watchdog independiente, protecciones y pruebas físicas. La pérdida de internet no debe interrumpir la lógica autónoma; política de fallo y arranque explícita. No conectar este motor a los GPIO antes de validar estas condiciones.
## Pruebas
g++ -std=c++17 -Wall -Wextra -Werror -Wno-misleading-indentation -pedantic firmware/plc/tests/test_plc.cpp -o /tmp/plc-test && /tmp/plc-test
Workflow PLC engine tests ejecuta pruebas en GitHub Actions. Esta fase no cambia el firmware activo ni Render.
