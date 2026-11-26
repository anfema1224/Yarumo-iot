# Yarumo IoT

Plataforma multiusuario de telemetría, telecontrol y automatización remota con ESP32.

## Alcance inicial
- Autenticación de usuarios y dispositivos con credenciales independientes.
- Registro y gestión de varios ESP32 por usuario, con aislamiento de acceso.
- Lectura de sensores analógicos y digitales, históricos y alarmas.
- Control remoto de cuatro relés con confirmación de estado.
- Editor de automatizaciones con comparadores, lógica booleana, temporizadores TON/TOF y pulsos.
- Ejecución local de reglas versionadas, incluso sin Internet.

## Hardware inicial
ESP32 Relay X4: relés GPIO32, 33, 25 y 26, previamente probados. Entradas propuestas DI: 13, 14, 16, 17, 19, 27; AI: 34, 35, 36, 39; I2C: 21/22. Validar físicamente entradas, polaridad y condiciones de arranque. GPIO a 3,3 V; proteger y acondicionar señales industriales.

## Arquitectura propuesta
Node.js, PostgreSQL, interfaz web y MQTT sobre TLS con ACL por dispositivo. Firmware Arduino/PlatformIO. Arranque seguro, caducidad de comandos, límites de activación, enclavamientos, auditoría y persistencia local. No sustituye protecciones independientes ni PLC de seguridad.

## Estado
Inicialización documental. No es todavía una aplicación funcional. No guardar contraseñas, tokens ni claves en el repositorio.
