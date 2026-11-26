# V0.5.0 — Transferencia remota (fase 1)
Rama de prueba; no desplegar hasta completar CI y migración. La versión V0.4.1 continúa disponible.
- El editor guarda una revisión por dispositivo, valida y compila el diagrama en el servidor.
- La transferencia usa el canal HTTPS autenticado existente, con un identificador de orden, vencimiento y confirmación. No inicia el programa: requiere STOP, todas las salidas apagadas y firmware compatible.
- El ESP32 valida el programa, lo instala en STOP y persiste su revisión en NVS. El portal muestra la revisión guardada, instalada y estado de la orden.
- STOP remoto usa el comando existente Apagar todos. RUN físico sigue siendo local; arranque siempre en STOP. RUN remoto y configuración de arranque automático se reservan para una fase posterior.
- La migración server/migrations/005-plc.sql debe aplicarse con el propietario del esquema iot, no con el rol de ejecución yarumo_iot. No modifica acceso. No registrar ni compartir secretos. Antes de desplegar, verificar las dos tablas y permisos del rol. El arranque del servidor valida sus permisos. Mantener el servicio actual en producción hasta la validación.
- Prueba inicial sin cargas: cargar V05 por USB u OTA conservando NVS, entrar al editor, seleccionar ESP32, guardar, transferir, verificar revisión instalada y estado ejecutado/STOP. Probar RUN local, STOP remoto, reinicio y recuperación de revisión. No activar maquinaria sin protecciones independientes.
