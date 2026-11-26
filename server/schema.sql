CREATE TABLE IF NOT EXISTS users (
 id uuid PRIMARY KEY, email text UNIQUE NOT NULL, password_hash text NOT NULL, created_at timestamptz NOT NULL DEFAULT now()
);
CREATE TABLE IF NOT EXISTS sessions (
 token_hash text PRIMARY KEY, user_id uuid NOT NULL REFERENCES users(id) ON DELETE CASCADE,
 expires_at timestamptz NOT NULL
);
CREATE TABLE IF NOT EXISTS devices (
 id uuid PRIMARY KEY, owner_id uuid NOT NULL REFERENCES users(id) ON DELETE CASCADE,
 name text NOT NULL, hardware_id text NOT NULL, token_hash text UNIQUE NOT NULL,
 last_seen timestamptz, last_sample timestamptz, snapshot jsonb, UNIQUE(owner_id,hardware_id)
);
CREATE TABLE IF NOT EXISTS readings (
 id bigserial PRIMARY KEY, device_id uuid NOT NULL REFERENCES devices(id) ON DELETE CASCADE,
 at timestamptz NOT NULL DEFAULT now(), data jsonb NOT NULL
);
CREATE INDEX IF NOT EXISTS readings_device_at ON readings(device_id,at DESC);
CREATE TABLE IF NOT EXISTS commands (
 id uuid PRIMARY KEY, device_id uuid NOT NULL REFERENCES devices(id) ON DELETE CASCADE,
 action text NOT NULL CHECK(action IN ('pulse','off','all')), relay integer NOT NULL, ms integer NOT NULL,
 state text NOT NULL DEFAULT 'queued' CHECK(state IN ('queued','delivered','executed','rejected','expired','unknown','cancelled')),
 created_at timestamptz NOT NULL DEFAULT now(), expires_at timestamptz NOT NULL, delivered_at timestamptz,
 finished_at timestamptz
);
CREATE INDEX IF NOT EXISTS commands_device_created ON commands(device_id,created_at DESC);
