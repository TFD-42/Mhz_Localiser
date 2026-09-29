CREATE TABLE products (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    manufacturer TEXT NOT NULL,
    model TEXT NOT NULL,
    product_category TEXT NOT NULL,
    exact_variant TEXT,
    region TEXT,
    official_product_url TEXT,
    created_at TEXT NOT NULL,
    updated_at TEXT NOT NULL
);

CREATE TABLE regulatory_notes (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    rf_profile_id INTEGER NOT NULL REFERENCES rf_profiles(id),
    jurisdiction TEXT,
    authority TEXT,
    permitted_use_summary TEXT,
    power_limit_summary TEXT,
    duty_cycle_summary TEXT,
    license_required INTEGER,
    official_url TEXT,
    verified_at TEXT,
    notes TEXT
);

CREATE TABLE research_log (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    query TEXT,
    url TEXT,
    result TEXT,
    rejection_reason TEXT,
    checked_at TEXT
);

CREATE TABLE rf_profiles (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    product_id INTEGER NOT NULL REFERENCES products(id),
    signal_type TEXT NOT NULL,
    frequency_min_hz REAL,
    frequency_max_hz REAL,
    center_frequency_hz REAL,
    channel TEXT,
    bandwidth_hz REAL,
    modulation TEXT,
    protocol TEXT,
    power_dbm REAL,
    power_type TEXT,
    duty_cycle TEXT,
    receive_or_transmit TEXT CHECK(receive_or_transmit IN ('receive','transmit','both')),
    confidence REAL CHECK(confidence BETWEEN 0 AND 1),
    verification_status TEXT CHECK(verification_status IN ('verified','partially_verified','conflicting','unverified','rejected')),
    notes TEXT,
    created_at TEXT NOT NULL
);

CREATE TABLE sources (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    rf_profile_id INTEGER NOT NULL REFERENCES rf_profiles(id),
    source_type TEXT,
    publisher TEXT,
    title TEXT,
    url TEXT,
    document_reference TEXT,
    accessed_at TEXT,
    quoted_text TEXT,
    source_reliability TEXT CHECK(source_reliability IN ('high','medium','low','rejected')),
    notes TEXT
);

CREATE TABLE sqlite_sequence(name,seq);

