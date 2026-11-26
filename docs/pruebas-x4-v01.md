# Prueba V0.1 — ESP32 Relay X4

Firmware de diagnóstico, sin backend ni control por Internet. Cargar solamente con los contactos de los relés desconectados de cargas. La prueba anterior validó la carga ArduinoOTA por Wi-Fi local y la secuencia de los cuatro relés según el usuario. Aún no se ha compilado ni ejecutado este firmware V0.1.

## Carga
1. Descargar firmware/Yarumo_X4_V01/Yarumo_X4_V01.ino. Mantener el archivo dentro de una carpeta con el mismo nombre.
2. Editar WIFI_SSID, WIFI_PASS y OTA_PASS únicamente en una copia local. Usar la contraseña actual para autorizar la transferencia; la contraseña del nuevo sketch se aplicará después del reinicio. No subir credenciales al repositorio público.
3. Seleccionar ESP32 Dev Module, esquema de particiones OTA que ya funcionó, puerto de red Yarumo-X4 y cargar.
4. Para diagnóstico conectar USB-TTL a UART0 con niveles de 3,3 V y GND común; monitor 115200, terminación de línea Nueva línea. No unir GPIO0 a GND al arrancar normalmente.

## Pruebas
- Estado de arranque: cuatro salidas apagadas. Perfil asume relés activos HIGH; confirmar sin cargas.
- DI1-6 (GPIO13,14,16,17,19,27): INPUT_PULLUP; conectar a GND de uno en uno. Esperar transición 1 a 0. Verificar que los pines estén físicamente expuestos antes de conectar.
- AI1-4 (GPIO34,35,36,39): ADC1, 12 bits. No dejar flotantes durante mediciones; usar una fuente segura de baja tensión con tierra común (por ejemplo potenciómetro entre 3,3 V y GND), nunca 5 V o 24 V. Lectura en mV aproximada; calibrar para cada sensor. El rango útil ADC puede ser menor que 3,3 V y saturarse. Los valores raw y mV corresponden a conversiones distintas.
- STATUS: lectura JSON de identificación, versión, Wi-Fi, DI, AI raw/mV y salidas ordenadas; no confirma posición física de contactos.
- PULSE 1 2000: activar relé 1 por dos segundos, máximo 10 segundos por comando. Repetir para relés 2, 3 y 4.
- OFF 1 / ALL OFF: apagar una/todas las salidas.
- RATE 2000: cambiar reporte serial a dos segundos y guardar en NVS.
- Verificar apagado al iniciar OTA y al detectar pérdida Wi-Fi; reconectar y comprobar detección OTA. No es un mecanismo de seguridad industrial.
- I2C 21/22 reservado, no habilitado todavía.

## Siguiente integración
Autenticación de usuario y dispositivo, TLS, protocolo de telemetría y órdenes con caducidad, ACK, motor local TON/TOF y políticas de desconexión. No confundir OTA local con OTA segura por Internet. Para producción: cambiar nombre por dispositivo, contraseña única fuerte, restringir acceso de red, firmware firmado y particiones con rollback validado.