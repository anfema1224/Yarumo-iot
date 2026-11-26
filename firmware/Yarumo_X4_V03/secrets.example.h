#pragma once
// Copiar como secrets.h en la misma carpeta que Yarumo_X4_V03.ino.
// Completar una sola vez. NO subir secrets.h a GitHub.
// Para sustituir una configuración existente, cambiar temporalmente a 1
// y después borrar secrets.h o volver a 0 antes de futuras actualizaciones.
#define YARUMO_REPLACE_CONFIG 0
#define YARUMO_WIFI_SSID "TU_RED_WIFI"
#define YARUMO_WIFI_PASS "TU_CLAVE_WIFI"
#define YARUMO_OTA_PASS "TU_CLAVE_OTA"
#define YARUMO_WEB_PASS "TU_CLAVE_WEB"
// Token vacío = nube deshabilitada. CA vacía = raíces públicas incluidas en firmware V0.3.2.
#define YARUMO_CLOUD_TOKEN ""
#define YARUMO_CLOUD_CA ""
// Para una CA PEM usar: #define YARUMO_CLOUD_CA R"PEM(...certificado...)PEM"
