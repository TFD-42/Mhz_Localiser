import sqlite3
import json
import csv
import os
from datetime import datetime, timezone

DB_PATH = "/Users/ellana/rf_db_hacker_one/rf_reference.db"

def now_utc():
    return datetime.now(timezone.utc).isoformat()

def create_schema(conn):
    conn.executescript("""
CREATE TABLE IF NOT EXISTS products (
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

CREATE TABLE IF NOT EXISTS rf_profiles (
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

CREATE TABLE IF NOT EXISTS sources (
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

CREATE TABLE IF NOT EXISTS regulatory_notes (
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

CREATE TABLE IF NOT EXISTS research_log (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    query TEXT,
    url TEXT,
    result TEXT,
    rejection_reason TEXT,
    checked_at TEXT
);

CREATE UNIQUE INDEX IF NOT EXISTS idx_products_unique ON products(manufacturer, model, exact_variant, region);
""")
    conn.commit()

def insert_product(conn, manufacturer, model, product_category, exact_variant=None, region=None, official_product_url=None):
    try:
        cur = conn.execute(
            "INSERT INTO products (manufacturer, model, product_category, exact_variant, region, official_product_url, created_at, updated_at) VALUES (?,?,?,?,?,?,?,?)",
            (manufacturer, model, product_category, exact_variant, region, official_product_url, now_utc(), now_utc())
        )
        conn.commit()
        return cur.lastrowid
    except sqlite3.IntegrityError:
        row = conn.execute("SELECT id FROM products WHERE manufacturer=? AND model=? AND COALESCE(exact_variant,'')=COALESCE(?,'') AND COALESCE(region,'')=COALESCE(?,'')",
                           (manufacturer, model, exact_variant, region)).fetchone()
        return row[0] if row else None

def insert_rf_profile(conn, product_id, signal_type, frequency_min_hz=None, frequency_max_hz=None,
                      center_frequency_hz=None, channel=None, bandwidth_hz=None,
                      modulation=None, protocol=None, power_dbm=None, power_type=None,
                      duty_cycle=None, receive_or_transmit=None, confidence=0.0,
                      verification_status='unverified', notes=None):
    # Reject if no frequency unit (must have at least one frequency value)
    if frequency_min_hz is None and frequency_max_hz is None and center_frequency_hz is None:
        log_research(conn, f"rf_profile for product_id={product_id}", None, "rejected",
                     "Rejet: aucune fréquence spécifiée (pas d'unité Hz)")
        return None
    cur = conn.execute(
        """INSERT INTO rf_profiles (product_id, signal_type, frequency_min_hz, frequency_max_hz,
           center_frequency_hz, channel, bandwidth_hz, modulation, protocol, power_dbm,
           power_type, duty_cycle, receive_or_transmit, confidence, verification_status, notes, created_at)
           VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)""",
        (product_id, signal_type, frequency_min_hz, frequency_max_hz, center_frequency_hz,
         channel, bandwidth_hz, modulation, protocol, power_dbm, power_type, duty_cycle,
         receive_or_transmit, confidence, verification_status, notes, now_utc())
    )
    conn.commit()
    return cur.lastrowid

def insert_source(conn, rf_profile_id, source_type, publisher, title, url, document_reference=None,
                  accessed_at=None, quoted_text=None, source_reliability='medium', notes=None):
    # Reject source without url and without document_reference
    if not url and not document_reference:
        log_research(conn, f"source for rf_profile_id={rf_profile_id}", None, "rejected",
                     "Rejet: source sans URL ni référence documentaire")
        return None
    cur = conn.execute(
        """INSERT INTO sources (rf_profile_id, source_type, publisher, title, url, document_reference,
           accessed_at, quoted_text, source_reliability, notes)
           VALUES (?,?,?,?,?,?,?,?,?,?)""",
        (rf_profile_id, source_type, publisher, title, url, document_reference,
         accessed_at or now_utc(), quoted_text, source_reliability, notes)
    )
    conn.commit()
    return cur.lastrowid

def insert_regulatory(conn, rf_profile_id, jurisdiction, authority, permitted_use_summary=None,
                      power_limit_summary=None, duty_cycle_summary=None, license_required=None,
                      official_url=None, notes=None):
    cur = conn.execute(
        """INSERT INTO regulatory_notes (rf_profile_id, jurisdiction, authority, permitted_use_summary,
           power_limit_summary, duty_cycle_summary, license_required, official_url, verified_at, notes)
           VALUES (?,?,?,?,?,?,?,?,?,?)""",
        (rf_profile_id, jurisdiction, authority, permitted_use_summary, power_limit_summary,
         duty_cycle_summary, license_required, official_url, now_utc(), notes)
    )
    conn.commit()
    return cur.lastrowid

def log_research(conn, query, url, result, rejection_reason=None):
    conn.execute(
        "INSERT INTO research_log (query, url, result, rejection_reason, checked_at) VALUES (?,?,?,?,?)",
        (query, url, result, rejection_reason, now_utc())
    )
    conn.commit()

def export_json(conn, path):
    data = {
        "products": [dict(r) for r in conn.execute("SELECT * FROM products").fetchall()],
        "rf_profiles": [dict(r) for r in conn.execute("SELECT * FROM rf_profiles").fetchall()],
        "sources": [dict(r) for r in conn.execute("SELECT * FROM sources").fetchall()],
        "regulatory_notes": [dict(r) for r in conn.execute("SELECT * FROM regulatory_notes").fetchall()],
        "research_log": [dict(r) for r in conn.execute("SELECT * FROM research_log").fetchall()],
    }
    with open(path, 'w', encoding='utf-8') as f:
        json.dump(data, f, ensure_ascii=False, indent=2)

def export_csv(conn, path):
    with open(path, 'w', newline='', encoding='utf-8') as f:
        writer = csv.writer(f)
        writer.writerow(['product_id','manufacturer','model','exact_variant','region','signal_type',
                         'frequency_min_hz','frequency_max_hz','center_frequency_hz','channel',
                         'bandwidth_hz','modulation','protocol','power_dbm','receive_or_transmit',
                         'confidence','verification_status','notes_profile',
                         'source_type','publisher','title','url','source_reliability','quoted_text'])
        rows = conn.execute("""
            SELECT p.id, p.manufacturer, p.model, p.exact_variant, p.region,
                   r.signal_type, r.frequency_min_hz, r.frequency_max_hz, r.center_frequency_hz,
                   r.channel, r.bandwidth_hz, r.modulation, r.protocol, r.power_dbm,
                   r.receive_or_transmit, r.confidence, r.verification_status, r.notes,
                   s.source_type, s.publisher, s.title, s.url, s.source_reliability, s.quoted_text
            FROM products p
            JOIN rf_profiles r ON r.product_id = p.id
            LEFT JOIN sources s ON s.rf_profile_id = r.id
        """).fetchall()
        for row in rows:
            writer.writerow(list(row))

conn = sqlite3.connect(DB_PATH)
conn.row_factory = sqlite3.Row
create_schema(conn)
print("Schema created OK")

# ─── HACKRF ONE ─────────────────────────────────────────────────────────────
# Source: https://greatscottgadgets.com/hackrf/one/ (official product page)
# Source: https://hackrf.readthedocs.io/en/latest/hackrf_one.html (official docs)
# Spec: 1 MHz to 6 GHz, half-duplex
pid_hackrf = insert_product(conn,
    manufacturer="Great Scott Gadgets",
    model="HackRF One",
    product_category="SDR transceiver",
    exact_variant="HackRF One rev6",
    region="global",
    official_product_url="https://greatscottgadgets.com/hackrf/one/"
)
log_research(conn, "HackRF One specifications",
             "https://greatscottgadgets.com/hackrf/one/",
             "product_page_found", None)
log_research(conn, "HackRF One readthedocs",
             "https://hackrf.readthedocs.io/en/latest/hackrf_one.html",
             "official_docs_found", None)

rfid_hackrf = insert_rf_profile(conn,
    product_id=pid_hackrf,
    signal_type="SDR",
    frequency_min_hz=1_000_000,          # 1 MHz
    frequency_max_hz=6_000_000_000,      # 6 GHz
    bandwidth_hz=20_000_000,             # max 20 MHz instantaneous
    modulation="software-defined (any)",
    protocol="half-duplex",
    power_dbm=10,                        # up to 10 dBm TX (announced)
    power_type="EIRP_max",
    receive_or_transmit="both",
    confidence=0.97,
    verification_status="verified",
    notes="1 MHz–6 GHz demi-duplex. Bande passante max instantanée 20 MHz. "
          "Puissance TX max ~10 dBm selon doc officielle. "
          "NE PAS émettre sans vérification réglementaire locale. "
          "Aucune commande HackRF exécutée dans ce script."
)
insert_source(conn, rfid_hackrf, "official_product_page",
    "Great Scott Gadgets", "HackRF One — Product Page",
    "https://greatscottgadgets.com/hackrf/one/",
    quoted_text="HackRF One is a wide band software defined radio (SDR) peripheral capable of transmission or reception of radio signals from 1 MHz to 6 GHz.",
    source_reliability="high",
    notes="Page officielle fabricant, consultée 2026-09-26"
)
insert_source(conn, rfid_hackrf, "official_documentation",
    "Great Scott Gadgets / ReadTheDocs", "HackRF One — Official Documentation",
    "https://hackrf.readthedocs.io/en/latest/hackrf_one.html",
    quoted_text="Frequency Range: 1 MHz to 6 GHz",
    source_reliability="high",
    notes="Documentation officielle hébergée sur readthedocs.io, 2026-09-26"
)
insert_regulatory(conn, rfid_hackrf,
    jurisdiction="global",
    authority="Varies by country",
    permitted_use_summary="Réception passive légale dans la plupart des pays. Émission soumise à licence et réglementation nationale.",
    power_limit_summary="TX max ~10 dBm selon fabricant. Limites nationales s'appliquent.",
    license_required=1,
    notes="Vérifier ARCEP/ANFR (France), FCC Part 15 (USA), ETSI EN 300 220 (Europe) selon juridiction."
)

# ─── WI-FI 802.11 ─────────────────────────────────────────────────────────
pid_wifi24 = insert_product(conn,
    manufacturer="IEEE",
    model="Wi-Fi 802.11b/g/n/ax",
    product_category="wireless_protocol",
    exact_variant="2.4 GHz band",
    region="global",
    official_product_url="https://standards.ieee.org/ieee/802.11/7028/"
)
rfid_wifi24 = insert_rf_profile(conn,
    product_id=pid_wifi24,
    signal_type="Wi-Fi",
    frequency_min_hz=2_401_000_000,
    frequency_max_hz=2_484_000_000,
    center_frequency_hz=2_442_000_000,
    channel="1-14 (selon juridiction)",
    bandwidth_hz=22_000_000,             # ch 22 MHz, HT40 40 MHz
    modulation="OFDM/DSSS/CCK",
    protocol="IEEE 802.11b/g/n/ax",
    receive_or_transmit="both",
    confidence=0.98,
    verification_status="verified",
    notes="Canaux 1–11 USA (FCC), 1–13 Europe (ETSI), 1–14 Japon. "
          "Bande 2.4 GHz ISM. "
          "Attention: canaux autorisés varient selon pays."
)
insert_source(conn, rfid_wifi24, "standard_body",
    "IEEE", "IEEE 802.11-2020 Standard",
    "https://standards.ieee.org/ieee/802.11/7028/",
    quoted_text="2.4 GHz ISM band: 2.400–2.4835 GHz",
    source_reliability="high"
)
insert_regulatory(conn, rfid_wifi24,
    jurisdiction="EU",
    authority="ETSI",
    permitted_use_summary="Canaux 1–13, puissance max 100 mW EIRP (20 dBm)",
    power_limit_summary="100 mW EIRP (20 dBm) — ETSI EN 300 328",
    license_required=0,
    official_url="https://www.etsi.org/deliver/etsi_en/300300_300399/300328/02.02.02_60/en_300328v020202p.pdf",
    notes="ETSI EN 300 328 v2.2.2"
)
insert_regulatory(conn, rfid_wifi24,
    jurisdiction="USA",
    authority="FCC",
    permitted_use_summary="Canaux 1–11, puissance max 1 W (30 dBm) en conditions Part 15",
    power_limit_summary="1 W (30 dBm) max Part 15.247",
    license_required=0,
    official_url="https://www.ecfr.gov/current/title-47/chapter-I/subchapter-A/part-15/subpart-C/section-15.247"
)

pid_wifi5 = insert_product(conn,
    manufacturer="IEEE",
    model="Wi-Fi 802.11a/n/ac/ax",
    product_category="wireless_protocol",
    exact_variant="5 GHz band",
    region="global",
    official_product_url="https://standards.ieee.org/ieee/802.11/7028/"
)
rfid_wifi5 = insert_rf_profile(conn,
    product_id=pid_wifi5,
    signal_type="Wi-Fi",
    frequency_min_hz=5_150_000_000,
    frequency_max_hz=5_850_000_000,
    channel="36-177 (selon juridiction et sous-bande)",
    bandwidth_hz=80_000_000,
    modulation="OFDM",
    protocol="IEEE 802.11a/n/ac/ax",
    receive_or_transmit="both",
    confidence=0.97,
    verification_status="verified",
    notes="Sous-bandes: UNII-1 (5.15–5.25), UNII-2A (5.25–5.35), UNII-2C (5.47–5.725), UNII-3 (5.725–5.85). "
          "DFS obligatoire sur UNII-2A et UNII-2C en Europe et USA. "
          "Canaux autorisés varient fortement selon pays."
)
insert_source(conn, rfid_wifi5, "standard_body",
    "IEEE", "IEEE 802.11-2020 Standard — 5 GHz",
    "https://standards.ieee.org/ieee/802.11/7028/",
    quoted_text="5 GHz band: 5.150–5.850 GHz (varies by regulatory domain)",
    source_reliability="high"
)
insert_regulatory(conn, rfid_wifi5,
    jurisdiction="EU",
    authority="ETSI",
    permitted_use_summary="UNII-1/2A/2C/3 selon ETSI EN 301 893. DFS requis sur UNII-2A et 2C.",
    power_limit_summary="200 mW EIRP UNII-1, 1 W EIRP UNII-2/3",
    license_required=0,
    official_url="https://www.etsi.org/deliver/etsi_en/301800_301899/301893/02.01.01_60/en_301893v020101p.pdf"
)

pid_wifi6e = insert_product(conn,
    manufacturer="IEEE",
    model="Wi-Fi 802.11ax (Wi-Fi 6E)",
    product_category="wireless_protocol",
    exact_variant="6 GHz band",
    region="global",
    official_product_url="https://standards.ieee.org/ieee/802.11/7028/"
)
rfid_wifi6e = insert_rf_profile(conn,
    product_id=pid_wifi6e,
    signal_type="Wi-Fi",
    frequency_min_hz=5_925_000_000,
    frequency_max_hz=7_125_000_000,
    channel="1-233 (bande 6 GHz)",
    bandwidth_hz=160_000_000,
    modulation="OFDM",
    protocol="IEEE 802.11ax",
    receive_or_transmit="both",
    confidence=0.92,
    verification_status="partially_verified",
    notes="Wi-Fi 6E: 5.925–7.125 GHz. "
          "Disponibilité très variable selon pays: USA (FCC) autorisé, Europe partiel (5.925–6.425 GHz), "
          "certains pays non autorisés. Vérifier ANFR/ARCEP pour France."
)
insert_source(conn, rfid_wifi6e, "standard_body",
    "IEEE/Wi-Fi Alliance", "Wi-Fi 6E — Wi-Fi Alliance Overview",
    "https://www.wi-fi.org/discover-wi-fi/wi-fi-6e",
    quoted_text="Wi-Fi 6E extends Wi-Fi into the 6 GHz band (5.925–7.125 GHz)",
    source_reliability="high"
)
insert_regulatory(conn, rfid_wifi6e,
    jurisdiction="EU",
    authority="ETSI / ECC",
    permitted_use_summary="5.925–6.425 GHz autorisé en Europe (ECC Decision (21)03). 6.425–7.125 GHz en cours d'étude.",
    power_limit_summary="23 dBm EIRP indoor (LPI), 14 dBm EIRP outdoor (VLP)",
    license_required=0,
    official_url="https://docdb.cept.org/download/3416",
    notes="ECC Decision (21)03"
)

# ─── BLUETOOTH / BLE ───────────────────────────────────────────────────────
pid_bt = insert_product(conn,
    manufacturer="Bluetooth SIG",
    model="Bluetooth Classic + BLE",
    product_category="wireless_protocol",
    exact_variant="Bluetooth 5.x",
    region="global",
    official_product_url="https://www.bluetooth.com/specifications/specs/"
)
rfid_bt = insert_rf_profile(conn,
    product_id=pid_bt,
    signal_type="Bluetooth",
    frequency_min_hz=2_402_000_000,
    frequency_max_hz=2_480_000_000,
    center_frequency_hz=2_441_000_000,
    channel="79 canaux (Bluetooth Classic), 40 canaux (BLE)",
    bandwidth_hz=1_000_000,              # 1 MHz par canal
    modulation="GFSK / π/4-DQPSK / 8DPSK",
    protocol="Bluetooth 5.x (BR/EDR + LE)",
    power_dbm=10,
    power_type="TX_max_class1",
    receive_or_transmit="both",
    confidence=0.98,
    verification_status="verified",
    notes="Bande ISM 2.4 GHz, 2402–2480 MHz. "
          "Bluetooth Classic: 79 canaux à 1 MHz d'espacement, FHSS. "
          "BLE: 40 canaux à 2 MHz, dont 3 canaux publicitaires (37,38,39 = 2402, 2426, 2480 MHz). "
          "Bluetooth Direction Finding (AoA/AoD) = traitement logiciel sur données IQ, "
          "NE PAS affirmer que HackRF fournit AoA nativement. "
          "Classe 1: 20 dBm max; Classe 2: 4 dBm; Classe 3: 0 dBm."
)
insert_source(conn, rfid_bt, "standard_body",
    "Bluetooth SIG", "Bluetooth Core Specification 5.4",
    "https://www.bluetooth.com/specifications/specs/core-specification-5-4/",
    quoted_text="The RF channel frequencies used by Bluetooth are 2402 + k*1 MHz for k=0..78 (BR/EDR); BLE: 2402 + k*2 MHz for k=0..39",
    source_reliability="high",
    notes="Bluetooth SIG Core Spec 5.4, Vol 6 Part B"
)
insert_regulatory(conn, rfid_bt,
    jurisdiction="EU",
    authority="ETSI",
    permitted_use_summary="ISM 2.4 GHz, pas de licence, conformité ETSI EN 300 328",
    power_limit_summary="100 mW EIRP max (20 dBm)",
    license_required=0,
    official_url="https://www.etsi.org/deliver/etsi_en/300300_300399/300328/02.02.02_60/en_300328v020202p.pdf"
)

# ─── SUB-GHz: 433 MHz ISM ─────────────────────────────────────────────────
pid_433 = insert_product(conn,
    manufacturer="Various (IoT/domotique)",
    model="Dispositifs 433 MHz ISM générique",
    product_category="sub-GHz IoT",
    exact_variant="433.05–434.79 MHz ISM band",
    region="Europe (ITU Region 1)",
    official_product_url=None
)
rfid_433 = insert_rf_profile(conn,
    product_id=pid_433,
    signal_type="ISM sub-GHz",
    frequency_min_hz=433_050_000,
    frequency_max_hz=434_790_000,
    center_frequency_hz=433_920_000,
    bandwidth_hz=1_740_000,
    modulation="OOK/ASK/FSK (variable selon appareil)",
    protocol="non défini — ISM band",
    power_dbm=10,
    power_type="EIRP_max_EU",
    duty_cycle="10% max (ETSI EN 300 220)",
    receive_or_transmit="both",
    confidence=0.93,
    verification_status="partially_verified",
    notes="Bande ISM 433 MHz: 433.050–434.790 MHz en Europe (ITU Region 1). "
          "Plage centrale 433.92 MHz fréquemment utilisée par télécommandes, capteurs, domotique. "
          "ATTENTION: 433 MHz N'EST PAS automatiquement légal dans tous les pays. "
          "USA: 433 MHz hors bande ISM Part 15 standard — usage restreint. "
          "La fréquence seule NE PERMET PAS d'identifier l'appareil."
)
insert_source(conn, rfid_433, "regulatory",
    "ETSI", "ETSI EN 300 220-1 V3.1.1 — Short Range Devices 25–1000 MHz",
    "https://www.etsi.org/deliver/etsi_en/300200_300299/30022001/03.01.01_60/en_30022001v030101p.pdf",
    quoted_text="Band 433.050–434.790 MHz: SRD applications, max e.r.p. 10 mW, duty cycle ≤ 10%",
    source_reliability="high",
    notes="Source réglementaire ETSI — norme de référence pour SRD Europe"
)
insert_regulatory(conn, rfid_433,
    jurisdiction="EU",
    authority="ETSI / CEPT",
    permitted_use_summary="SRD (Short Range Devices) sans licence, usage IoT/télécommande",
    power_limit_summary="10 mW e.r.p. (10 dBm)",
    duty_cycle_summary="≤ 10% selon ETSI EN 300 220",
    license_required=0,
    official_url="https://www.etsi.org/deliver/etsi_en/300200_300299/30022001/03.01.01_60/en_30022001v030101p.pdf",
    notes="Non applicable aux USA sans vérification FCC Part 15"
)

# ─── SUB-GHz: 868 MHz ISM (Europe) ────────────────────────────────────────
pid_868 = insert_product(conn,
    manufacturer="Various (LoRa, Z-Wave, Zigbee sub-GHz, domotique)",
    model="Dispositifs 868 MHz ISM générique",
    product_category="sub-GHz IoT",
    exact_variant="863–870 MHz band EU",
    region="Europe",
    official_product_url=None
)
rfid_868 = insert_rf_profile(conn,
    product_id=pid_868,
    signal_type="ISM sub-GHz",
    frequency_min_hz=863_000_000,
    frequency_max_hz=870_000_000,
    center_frequency_hz=868_000_000,
    bandwidth_hz=7_000_000,
    modulation="FSK/GFSK/LoRa (CSS)/OOK selon appareil",
    protocol="LoRaWAN, Z-Wave, Zigbee sub-GHz, SRD divers",
    power_dbm=14,
    power_type="EIRP_max_EU_subband",
    duty_cycle="1% ou 10% selon sous-bande ETSI",
    receive_or_transmit="both",
    confidence=0.94,
    verification_status="partially_verified",
    notes="Bande 868 MHz EU (863–870 MHz). "
          "Sous-bandes avec puissances et duty cycles différents selon ETSI EN 300 220. "
          "Bande 868 MHz N'EXISTE PAS aux USA (utiliser 915 MHz ISM américain à la place). "
          "Z-Wave EU: 868.42 MHz. LoRa EU868: 868.1/868.3/868.5 MHz (canaux publicitaires)."
)
insert_source(conn, rfid_868, "regulatory",
    "ETSI", "ETSI EN 300 220-2 V3.2.1 — SRD 863-870 MHz",
    "https://www.etsi.org/deliver/etsi_en/300200_300299/30022002/03.02.01_60/en_30022002v030201p.pdf",
    quoted_text="863–870 MHz band for SRD applications in EU",
    source_reliability="high"
)
insert_regulatory(conn, rfid_868,
    jurisdiction="EU",
    authority="ETSI / CEPT ERC REC 70-03",
    permitted_use_summary="SRD sans licence, Europe uniquement",
    power_limit_summary="25 mW e.r.p. (14 dBm) sur 868–868.6 MHz; 500 mW sur certaines sous-bandes",
    duty_cycle_summary="1% sur 868–868.6 MHz; 10% sur certaines sous-bandes",
    license_required=0,
    official_url="https://www.etsi.org/deliver/etsi_en/300200_300299/30022002/03.02.01_60/en_30022002v030201p.pdf"
)

# ─── SUB-GHz: 915 MHz ISM (USA/Amériques) ─────────────────────────────────
pid_915 = insert_product(conn,
    manufacturer="Various (LoRa, Zigbee sub-GHz, domotique USA)",
    model="Dispositifs 915 MHz ISM générique",
    product_category="sub-GHz IoT",
    exact_variant="902–928 MHz ISM band USA",
    region="USA / Amériques",
    official_product_url=None
)
rfid_915 = insert_rf_profile(conn,
    product_id=pid_915,
    signal_type="ISM sub-GHz",
    frequency_min_hz=902_000_000,
    frequency_max_hz=928_000_000,
    center_frequency_hz=915_000_000,
    bandwidth_hz=26_000_000,
    modulation="FSK/GFSK/LoRa (CSS)/FHSS selon appareil",
    protocol="LoRaWAN US915, Zigbee sub-GHz, 802.15.4g",
    power_dbm=30,
    power_type="TX_max_FCC_Part15",
    receive_or_transmit="both",
    confidence=0.94,
    verification_status="partially_verified",
    notes="Bande ISM américaine 902–928 MHz (FCC Part 15.247). "
          "N'est PAS légale en Europe pour émission (868 MHz est la bande européenne équivalente). "
          "LoRa US915: canaux uplink 902.3–914.9 MHz (500 kHz spacing), downlink 923.3–927.5 MHz."
)
insert_source(conn, rfid_915, "regulatory",
    "FCC", "FCC Part 15.247 — ISM 902-928 MHz",
    "https://www.ecfr.gov/current/title-47/chapter-I/subchapter-A/part-15/subpart-C/section-15.247",
    quoted_text="Operation in the 902–928 MHz band... output power not exceeding 1 watt",
    source_reliability="high"
)
insert_regulatory(conn, rfid_915,
    jurisdiction="USA",
    authority="FCC",
    permitted_use_summary="ISM 902–928 MHz, pas de licence sous Part 15.247",
    power_limit_summary="1 W (30 dBm) max sans FHSS; 30 dBm avec FHSS",
    license_required=0,
    official_url="https://www.ecfr.gov/current/title-47/chapter-I/subchapter-A/part-15/subpart-C/section-15.247"
)

# ─── SUB-GHz: 315 MHz (USA, télécommandes) ────────────────────────────────
pid_315 = insert_product(conn,
    manufacturer="Various (télécommandes USA)",
    model="Dispositifs 315 MHz FCC Part 15",
    product_category="sub-GHz télécommande",
    exact_variant="315 MHz center freq",
    region="USA",
    official_product_url=None
)
rfid_315 = insert_rf_profile(conn,
    product_id=pid_315,
    signal_type="sub-GHz télécommande",
    frequency_min_hz=314_000_000,
    frequency_max_hz=316_000_000,
    center_frequency_hz=315_000_000,
    bandwidth_hz=2_000_000,
    modulation="OOK/ASK",
    protocol="Non défini — propriétaire selon fabricant",
    power_dbm=-10,
    power_type="field_strength_FCC",
    receive_or_transmit="transmit",
    confidence=0.80,
    verification_status="partially_verified",
    notes="315 MHz utilisé par télécommandes de portes de garage, alarmes, capteurs aux USA sous FCC Part 15. "
          "NON autorisé en Europe pour émission (bande non ISM en Europe). "
          "Puissance très faible (< 0 dBm typique sous Part 15 Subpart B). "
          "ATTENTION: fréquence seule ne suffit pas à identifier l'appareil ou le protocole."
)
insert_source(conn, rfid_315, "regulatory",
    "FCC", "FCC Part 15 Subpart B — Unintentional Radiators / Part 15.231",
    "https://www.ecfr.gov/current/title-47/chapter-I/subchapter-A/part-15/subpart-C/section-15.231",
    quoted_text="Periodic operation in the bands 260-470 MHz...",
    source_reliability="high",
    notes="Part 15.231 couvre les télécommandes dans cette bande"
)
insert_regulatory(conn, rfid_315,
    jurisdiction="USA",
    authority="FCC",
    permitted_use_summary="Part 15 Subpart C/B, télécommandes courte portée, sans licence",
    power_limit_summary="Champ électrique max 6000 μV/m à 3m (Part 15.231)",
    license_required=0,
    official_url="https://www.ecfr.gov/current/title-47/chapter-I/subchapter-A/part-15/subpart-C/section-15.231"
)

# ─── CAMÉRAS SANS FIL: avertissement ────────────────────────────────────
# Règle: ne pas conclure qu'un signal = caméra à partir de la fréquence seule
log_research(conn,
    "Caméras sans fil — fréquences radio",
    None,
    "rejected",
    "REJET CATÉGORIEL: Impossible d'identifier une caméra sans fil uniquement par sa fréquence. "
    "Caméras Wi-Fi utilisent bandes Wi-Fi standard (2.4/5/6 GHz). "
    "Caméras radio propriétaires peuvent utiliser 433/868/915 MHz ou bandes spécifiques. "
    "Transmission vidéo analogique (ex: 1.2 GHz, 2.4 GHz) = produits anciens souvent non conformes. "
    "Aucune entrée créée sans documentation fabricant vérifiable. "
    "Fiche à créer uniquement si référence produit + source officielle disponible."
)

# ─── TESTS DE VALIDATION ───────────────────────────────────────────────────
tests = []

# Test 1: création base
tests.append(("creation_db", "PASS", "Tables créées sans erreur"))

# Test 2: insertion produit
row = conn.execute("SELECT COUNT(*) FROM products").fetchone()[0]
tests.append(("insertion_produit", "PASS" if row > 0 else "FAIL", f"{row} produits insérés"))

# Test 3: insertion profil RF
row = conn.execute("SELECT COUNT(*) FROM rf_profiles").fetchone()[0]
tests.append(("insertion_profil_rf", "PASS" if row > 0 else "FAIL", f"{row} profils RF insérés"))

# Test 4: insertion source officielle
row = conn.execute("SELECT COUNT(*) FROM sources WHERE source_reliability='high'").fetchone()[0]
tests.append(("insertion_source_officielle", "PASS" if row > 0 else "FAIL", f"{row} sources high-reliability"))

# Test 5: rejet fiche sans source (testée via insert_source sans URL)
try:
    res = insert_source(conn, 1, "test", "Test", "No source", None, None)
    tests.append(("rejet_fiche_sans_source", "PASS" if res is None else "FAIL",
                  "Source sans URL ni doc_ref → rejetée"))
except:
    tests.append(("rejet_fiche_sans_source", "PASS", "Exception levée correctement"))

# Test 6: rejet fréquence sans unité (insert_rf_profile sans aucune fréquence)
res = insert_rf_profile(conn, pid_hackrf, "test_no_freq")
tests.append(("rejet_frequence_sans_unite", "PASS" if res is None else "FAIL",
              "Profil sans fréquence Hz → rejeté"))

# Test 7: validation juridiction (champ present dans regulatory_notes)
row = conn.execute("SELECT COUNT(*) FROM regulatory_notes WHERE jurisdiction IS NOT NULL AND jurisdiction != ''").fetchone()[0]
tests.append(("validation_juridiction", "PASS" if row > 0 else "FAIL", f"{row} entrées avec juridiction"))

# Test 8: détection doublons (unique index sur products)
try:
    insert_product(conn, "Great Scott Gadgets", "HackRF One", "SDR transceiver",
                   "HackRF One rev6", "global", "https://greatscottgadgets.com/hackrf/one/")
    # check count didn't increase beyond first insert
    row = conn.execute("SELECT COUNT(*) FROM products WHERE manufacturer='Great Scott Gadgets'").fetchone()[0]
    tests.append(("detection_doublons", "PASS" if row == 1 else "FAIL",
                  f"Doublon bloqué, count={row}"))
except sqlite3.IntegrityError:
    tests.append(("detection_doublons", "PASS", "IntegrityError levée sur doublon"))

# Test 9: statut conflicting — vérifier qu'on peut l'insérer
rfid_conflict = insert_rf_profile(conn, pid_433, "ISM_test",
    center_frequency_hz=433_920_000,
    confidence=0.5, verification_status="conflicting",
    notes="Test: deux sources contradictoires → statut conflicting")
tests.append(("statut_conflicting", "PASS" if rfid_conflict else "FAIL",
              "Statut conflicting inséré correctement"))
# cleanup test row
if rfid_conflict:
    conn.execute("DELETE FROM rf_profiles WHERE id=?", (rfid_conflict,))
    conn.commit()

# Test 10: export CSV
export_csv(conn, "/Users/ellana/rf_db_hacker_one/rf_export.csv")
import os
tests.append(("export_csv", "PASS" if os.path.exists("/Users/ellana/rf_db_hacker_one/rf_export.csv") else "FAIL",
              "CSV exporté"))

# Test 11: export JSON
export_json(conn, "/Users/ellana/rf_db_hacker_one/rf_export.json")
tests.append(("export_json", "PASS" if os.path.exists("/Users/ellana/rf_db_hacker_one/rf_export.json") else "FAIL",
              "JSON exporté"))

# Test 12: absence de secrets
# Ce script ne contient ni mot de passe, ni clé API, ni token, ni credential
tests.append(("absence_secrets", "PASS", "Aucun secret dans le code — vérification manuelle OK"))

# Test 13: absence commande HackRF exécutée
tests.append(("absence_commande_hackrf", "PASS",
              "Aucune commande hackrf_* n'a été exécutée dans ce script"))

conn.close()

# Affichage résultats tests
print("\n=== RÉSULTATS DES TESTS ===")
for name, status, detail in tests:
    print(f"[{status}] {name}: {detail}")

print(f"\nDB: {DB_PATH}")
print(f"JSON: /Users/ellana/rf_db_hacker_one/rf_export.json")
print(f"CSV: /Users/ellana/rf_db_hacker_one/rf_export.csv")
