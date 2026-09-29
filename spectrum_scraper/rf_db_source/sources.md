# Sources retenues — rf_reference.db

Date de consultation : 2026-09-26 UTC

---

## Sources de niveau HIGH (officielles, vérifiées)

### 1. HackRF One — Page produit officielle
- **Éditeur** : Great Scott Gadgets
- **Titre** : HackRF One — Product Page
- **URL** : https://greatscottgadgets.com/hackrf/one/
- **Fiabilité** : HIGH
- **Citation** : "HackRF One is a wide band software defined radio (SDR) peripheral capable of transmission or reception of radio signals from 1 MHz to 6 GHz."
- **Justifie** : Plage de fréquences 1 MHz – 6 GHz, mode half-duplex

### 2. HackRF One — Documentation officielle ReadTheDocs
- **Éditeur** : Great Scott Gadgets / ReadTheDocs
- **Titre** : HackRF One — Official Documentation
- **URL** : https://hackrf.readthedocs.io/en/latest/hackrf_one.html
- **Fiabilité** : HIGH
- **Citation** : "Frequency Range: 1 MHz to 6 GHz" ; "Maximum input power: -5 dBm. Exceeding -5 dBm can result in permanent damage!"
- **Justifie** : Plage fréquences, puissance max entrée, puissance TX variable par bande

### 3. IEEE 802.11-2020 — Wi-Fi standard
- **Éditeur** : IEEE
- **Titre** : IEEE 802.11-2020 Standard
- **URL** : https://standards.ieee.org/ieee/802.11/7028/
- **Fiabilité** : HIGH
- **Citation** : "2.4 GHz ISM band: 2.400–2.4835 GHz" ; "5 GHz band: 5.150–5.850 GHz (varies by regulatory domain)"
- **Justifie** : Bandes Wi-Fi 2.4 GHz et 5 GHz

### 4. Wi-Fi Alliance — Wi-Fi 6E overview
- **Éditeur** : IEEE / Wi-Fi Alliance
- **Titre** : Wi-Fi 6E — Wi-Fi Alliance Overview
- **URL** : https://www.wi-fi.org/discover-wi-fi/wi-fi-6e
- **Fiabilité** : HIGH
- **Citation** : "Wi-Fi 6E extends Wi-Fi into the 6 GHz band (5.925–7.125 GHz)"
- **Justifie** : Bande Wi-Fi 6 GHz (Wi-Fi 6E)

### 5. Bluetooth Core Specification 5.4
- **Éditeur** : Bluetooth SIG
- **Titre** : Bluetooth Core Specification 5.4
- **URL** : https://www.bluetooth.com/specifications/specs/core-specification-5-4/
- **Fiabilité** : HIGH
- **Citation** : "The RF channel frequencies used by Bluetooth are 2402 + k*1 MHz for k=0..78 (BR/EDR); BLE: 2402 + k*2 MHz for k=0..39"
- **Justifie** : Canaux Bluetooth Classic et BLE, bande 2.4 GHz

### 6. ETSI EN 300 220-1 V3.1.1 — SRD 25–1000 MHz
- **Éditeur** : ETSI
- **Titre** : ETSI EN 300 220-1 V3.1.1 — Short Range Devices 25–1000 MHz
- **URL** : https://www.etsi.org/deliver/etsi_en/300200_300299/30022001/03.01.01_60/en_30022001v030101p.pdf
- **Fiabilité** : HIGH
- **Citation** : "Band 433.050–434.790 MHz: SRD applications, max e.r.p. 10 mW, duty cycle ≤ 10%"
- **Justifie** : Bande 433 MHz ISM Europe, puissance et duty cycle

### 7. ETSI EN 300 220-2 V3.2.1 — SRD 863–870 MHz
- **Éditeur** : ETSI
- **Titre** : ETSI EN 300 220-2 V3.2.1 — SRD 863–870 MHz
- **URL** : https://www.etsi.org/deliver/etsi_en/300200_300299/30022002/03.02.01_60/en_30022002v030201p.pdf
- **Fiabilité** : HIGH
- **Justifie** : Bande 868 MHz ISM Europe

### 8. ETSI EN 300 328 V2.2.2 — Wi-Fi / Bluetooth 2.4 GHz
- **Éditeur** : ETSI
- **Titre** : ETSI EN 300 328 V2.2.2 — Wideband transmission systems 2.4 GHz
- **URL** : https://www.etsi.org/deliver/etsi_en/300300_300399/300328/02.02.02_60/en_300328v020202p.pdf
- **Fiabilité** : HIGH
- **Justifie** : Puissance max Wi-Fi et Bluetooth en Europe (100 mW EIRP)

### 9. ETSI EN 301 893 V2.1.1 — Wi-Fi 5 GHz
- **Éditeur** : ETSI
- **Titre** : ETSI EN 301 893 V2.1.1 — Broadband Radio Access Networks 5 GHz
- **URL** : https://www.etsi.org/deliver/etsi_en/301800_301899/301893/02.01.01_60/en_301893v020101p.pdf
- **Fiabilité** : HIGH
- **Justifie** : Bande Wi-Fi 5 GHz Europe, DFS, puissances

### 10. ECC Decision (21)03 — Wi-Fi 6 GHz Europe
- **Éditeur** : ECC (CEPT)
- **Titre** : ECC Decision (21)03 — 5.925–6.425 GHz
- **URL** : https://docdb.cept.org/download/3416
- **Fiabilité** : HIGH
- **Justifie** : Autorisation Wi-Fi 6E en Europe (5.925–6.425 GHz)

### 11. FCC Part 15.247 — ISM 902–928 MHz USA
- **Éditeur** : FCC
- **Titre** : FCC Part 15.247 — ISM 902-928 MHz
- **URL** : https://www.ecfr.gov/current/title-47/chapter-I/subchapter-A/part-15/subpart-C/section-15.247
- **Fiabilité** : HIGH
- **Citation** : "Operation in the 902–928 MHz band... output power not exceeding 1 watt"
- **Justifie** : Bande ISM 915 MHz USA

### 12. FCC Part 15.231 — Télécommandes 315 MHz USA
- **Éditeur** : FCC
- **Titre** : FCC Part 15.231 — Periodic operation 260–470 MHz
- **URL** : https://www.ecfr.gov/current/title-47/chapter-I/subchapter-A/part-15/subpart-C/section-15.231
- **Fiabilité** : HIGH
- **Justifie** : Bande 315 MHz USA pour télécommandes

### 13. FCC Part 15 — Wi-Fi 2.4 GHz USA
- **Éditeur** : FCC
- **Titre** : FCC Part 15.247 — Wi-Fi 2.4 GHz
- **URL** : https://www.ecfr.gov/current/title-47/chapter-I/subchapter-A/part-15/subpart-C/section-15.247
- **Fiabilité** : HIGH
- **Justifie** : Puissance max Wi-Fi 2.4 GHz USA

---

## Sources rejetées

### R1. Caméras sans fil — fréquences génériques
- **Raison** : Impossible d'identifier une caméra sans fil uniquement par sa fréquence. Aucune référence fabricant + source officielle disponible pour une entrée générique.

### R2. Forums, Reddit, blogs
- **Raison** : Non consulté. Sources exclues par règle de validation.

### R3. Sites SEO sans référence fabricant
- **Raison** : Non consulté. Sources exclues par règle de validation.

---

## Informations restant à vérifier

1. **HackRF One TX power par bande** : la documentation readthedocs indique des valeurs variables (5–15 dBm selon bande). La page produit officielle ne donne pas de valeur précise. Vérifier le datasheet RF du module MAX5865/RFFC5072 pour confirmation.
2. **Wi-Fi HaLow (802.11ah)** : bande 900 MHz, à documenter avec source IEEE officielle. Non inclus dans cette version par manque de temps.
3. **Z-Wave France** : fréquence Z-Wave EU 868.42 MHz partiellement vérifiée. Vérifier la déclaration de conformité d'un produit Z-Wave FCC/CE spécifique.
4. **LoRaWAN EU868 canaux exacts** : vérifier avec LoRa Alliance Regional Parameters v2.1. Non inclus — source officielle à consulter.
5. **315 MHz hors USA** : usage en dehors des USA non vérifié. Marquer comme "hors périmètre" pour les pays sans réglementation explicite.
6. **Wi-Fi 6 GHz France spécifiquement** : vérifier si l'ANFR a publié une décision d'application nationale de ECC (21)03.
7. **Caméras sans fil propriétaires** : aucune entrée créée. Nécessite une référence produit + déclaration CE/FCC vérifiable.
