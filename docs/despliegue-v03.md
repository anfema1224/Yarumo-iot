# V0.3 Cloud — banco de pruebas
Estado: prototipo escrito, pendiente de pruebas de integración, compilación ESP32 y despliegue. No modificar V0.2 en main.

## Arquitectura
Node.js 22+, Express, PostgreSQL dedicado. HTTPS saliente desde ESP32, sin MQTT en esta fase. Cada cuenta tiene sus dispositivos; cada dispositivo tiene token aleatorio individual (solo se guarda su hash en BD). Registro por invitación. Cookies Secure/HttpOnly/SameSite Strict. No publicar secretos.

## Render
- Repositorio anfema1224/Yarumo-iot; rama feature/v03-cloud.
- Servicio web Node, build: npm install && npm test; start: npm start.
- DATABASE_URL: conexión PostgreSQL exclusiva de IoT, con TLS si es externa. REGISTRATION_CODE: secreto aleatorio de 24+ caracteres, configurar fuera de GitHub. PORT administrado por Render.
- No reutilizar yarumo-acceso-db ni maiky-pay-db. La creación de yarumo-iot-db en plan free fue rechazada por Render por existir una base gratuita activa. No se contrataron recursos de pago.
- Comprobar /api/health antes de registrar usuarios. El nivel gratuito puede suspenderse por inactividad; no usar para control crítico.

## Firmware
1. Instalar ArduinoJson 7 en Arduino IDE. Usar ESP32 Dev Module, tabla OTA compatible y una copia local de firmware/Yarumo_X4_V03/Yarumo_X4_V03.ino.
2. Conservar Wi-Fi, OTA y contraseña web V0.2. Registrar cuenta con invitación y vincular la MAC de laboratorio (sin separadores, 12 caracteres).
3. Configurar CLOUD_URL (https://dominio/api/device/poll), CLOUD_TOKEN (64 caracteres hexadecimales devueltos una sola vez) y CLOUD_CA (certificado PEM de la CA raíz comprobada de ese dominio). Sin CA válida o sincronización NTP no se habilita la comunicación remota. No usar setInsecure ni publicar tokens.
4. Cargar por OTA local. Confirmar panel local y presencia en cloud.
5. Sin cargas conectadas, verificar pulsos, apagado, caducidad, pérdida de Internet, revocación de credencial y reinicio. Estado reportado es orden GPIO, no realimentación de contacto.

## Contrato y límites
POST /api/device/poll con Authorization: Bearer TOKEN, JSON {snapshot:{device,version,di,ai_raw,ai_mv,relay_cmd,remaining_ms},ack?:{id,state}}. Respuesta {command:null|{id,action,relay,ms,ttl_ms}}.
Las órdenes pulse/off/all caducan en 10 s, se entregan como máximo una vez; un resultado perdido queda unknown, no se reenvía automáticamente. Los pulsos se limitan a 10 s y se apagan con pérdida Wi-Fi; el control por nube se ejecuta en el loop principal y la tarea de temporización permanece separada. No se garantiza tiempo real. No usar con cargas industriales hasta completar validación, enclavamientos y protecciones independientes. 
El historial conserva 24 h de muestras a intervalos de aproximadamente 15 s; las últimas 120 se ofrecen en la API. El panel muestra 30 recientes. Las reglas TON/TOF, horarios, OTA desde Internet, alarmas y gestión de roles aún no están implementadas.

## Pruebas pendientes
- npm test (validación de formato de mensajes); compilación firmware.
- Dos cuentas: acceso cruzado, comandos, token revocado, sesión caducada, registro inválido.
- Simulación de órdenes expiradas, respuesta perdida, ACK, caída de red, cargas desconectadas, reinicio/OTA.
- Verificar uso de memoria, estabilidad HTTP, concurrencia y certificados TLS. Revisar dependencias y añadir pruebas de integración PostgreSQL antes de producción.
