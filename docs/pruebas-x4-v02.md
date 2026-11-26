# Yarumo IoT V0.2 — Panel local

Descargar `firmware/Yarumo_X4_V02/Yarumo_X4_V02.ino` en carpeta de igual nombre. Editar únicamente copia local: WIFI_SSID, WIFI_PASS, OTA_PASS (conservar la actual si se desea) y WEB_PASS (12 caracteres como mínimo, distinta y no predeterminada). Usuario web: admin. No publicar credenciales.

Cargar por ArduinoOTA desde ESP32 Dev Module usando el esquema de particiones OTA ya validado. Autorizar la transferencia con la contraseña OTA del firmware actualmente instalado. Abrir http://IP_DEL_ESP32/ desde la misma red Wi-Fi; última IP observada 192.168.20.56, puede cambiar. El panel usa HTTP Digest, no HTTPS; exclusivamente banco de pruebas en red privada de confianza, sin redirección de puertos ni acceso desde Internet. Si no se configura WEB_PASS, el panel no se inicia.

## Aceptación
- Compilar y verificar que arranque, conserve OTA y publique IP.
- Iniciar sesión; comprobar que una solicitud no autenticada no controla salidas.
- Verificar DI1–6 al unir individualmente GPIO a GND; comprobar AI1–4 con señales seguras de 0–3,3 V y tierra común; ADC requiere calibración.
- Sin cargas en contactos, pulsar cada relé durante 2 segundos y comprobar apagado. Máximo 10 s por pulso, no admite prolongación de un pulso activo. Temporizadores en tarea separada del servidor HTTP. No es garantía de tiempo real ni seguridad industrial.
- Probar botón apagar todos, pérdida Wi-Fi, reconexión y OTA. El estado mostrado es la orden GPIO, no retroalimentación física.
- Mantiene JSON serial y comandos V0.1. No incluye todavía motor de reglas, servidor de Internet, HTTPS ni usuarios múltiples.

Estado: código publicado y revisado estáticamente; falta compilación en Arduino IDE y prueba física.
