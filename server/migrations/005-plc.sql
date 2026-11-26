-- Apply as owner of iot schema, not as runtime role. No change to acceso.
BEGIN;
CREATE TABLE IF NOT EXISTS iot.plc_programs (
 device_id uuid PRIMARY KEY REFERENCES iot.devices(id) ON DELETE CASCADE,
 revision uuid NOT NULL UNIQUE, graph jsonb NOT NULL, program jsonb NOT NULL,
 updated_at timestamptz NOT NULL DEFAULT now()
);
CREATE TABLE IF NOT EXISTS iot.plc_jobs (
 id uuid PRIMARY KEY, device_id uuid NOT NULL REFERENCES iot.devices(id) ON DELETE CASCADE,
 revision uuid NOT NULL, program jsonb NOT NULL,
 state text NOT NULL CHECK(state IN ('queued','delivered','executed','rejected','expired','unknown','cancelled')),
 created_at timestamptz NOT NULL DEFAULT now(), expires_at timestamptz NOT NULL, delivered_at timestamptz, finished_at timestamptz
);
CREATE INDEX IF NOT EXISTS plc_jobs_device_created ON iot.plc_jobs(device_id,created_at DESC);
REVOKE ALL ON iot.plc_programs,iot.plc_jobs FROM PUBLIC;
GRANT SELECT,INSERT,UPDATE,DELETE ON iot.plc_programs,iot.plc_jobs TO yarumo_iot;
COMMIT;
