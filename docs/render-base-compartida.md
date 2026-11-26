# Render anfemasena — PostgreSQL compartido

- Base: yarumo-db, PostgreSQL 16, plan gratuito, Oregon.
- Servicio: yarumo-iot, plan gratuito, rama feature/v03-cloud, autodeploy desactivado.
- URL: https://yarumo-iot.onrender.com
- No se habilita acceso externo a la base ni se modifican servicios UTP.

## Configuración inicial (solo Dashboard de Render)
Copiar la Internal Database URL de yarumo-db, no enviarla por chat ni subirla al repositorio. En Environment del servicio yarumo-iot definir:
1. BOOTSTRAP_DATABASE_URL: URL interna original (administrador, solo temporal).
2. DATABASE_URL: la misma URL interna sustituyendo usuario por yarumo_iot y contraseña por una nueva aleatoria de 24+ caracteres (hex recomendado).
3. REGISTRATION_CODE: otra cadena aleatoria independiente de 24+ caracteres.

Guardar y desplegar. El arranque crea los esquemas iot y acceso, tablas IoT y roles separados en una transacción. El rol yarumo_acceso permanece NOLOGIN hasta migrar la aplicación de acceso. El rol yarumo_iot tiene permisos CRUD sobre tablas del esquema iot, secuencia y USAGE, sin DDL ni acceso al otro esquema. La propiedad de esquemas permanece en el administrador. Si el usuario administrador no tiene CREATEROLE, la inicialización falla sin habilitar el control.

Comprobar logs y GET /api/health: status bootstrap_complete_remove_admin_secret. Borrar BOOTSTRAP_DATABASE_URL de Environment y volver a desplegar. El arranque normal verifica los permisos y las tablas. Solo si GET /api/health responde ok:true y version:0.3.0 registrar una cuenta de pruebas con REGISTRATION_CODE. No publicar ese código.
Si el estado es bootstrap_failed o runtime_configuration_failed, revisar logs sin exponer secretos. No ejecutar el bootstrap contra otra base. No cambiar el token de acceso ni activar yarumo_acceso hasta revisar sus migraciones. El bootstrap no restablece contraseñas de roles ya creados.

## Validaciones pendientes
La aplicación se creó en Render en modo configuración. No se ha inicializado la base ni probado el firmware cloud. Verificar aislamiento, credenciales, cuentas separadas y órdenes de relé sin cargas. El plan gratuito vence el 25/12/2026 hora Colombia; respaldar datos y planificar una base persistente antes del vencimiento.
