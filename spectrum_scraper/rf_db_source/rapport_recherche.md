# Rapport de recherche — Base de données RF passive

**Date** : 2026-09-26 UTC  
**Durée** : < 15 minutes  
**Agent** : Préparation technique cartographie radio passive  

---

## Résumé exécutif

Base de données SQLite `rf_reference.db` créée avec 9 produits/technologies, 9 profils RF, 10 sources officielles, 10 notes réglementaires. Tous les tests de validation passent (13/13). Aucune commande HackRF exécutée. Aucun secret dans les fichiers.

---

## Entrées validées

---

### Entrée 1 — HackRF One

**Produit** : HackRF One  
**Fabricant** : Great Scott Gadgets  
**Modèle exact** : HackRF One rev6  
**Catégorie** : SDR transceiver  
**Pays/région** : Global  
**Fréquence ou plage** : 1 MHz – 6 GHz  
**Unité normalisée** : 1 000 000 Hz – 6 000 000 000 Hz  
**Type de signal** : SDR (software-defined radio)  
**Protocole/modulation** : Configurable par logiciel, half-duplex  
**Puissance** : TX max ~10–15 dBm selon bande (variable), entrée max -5 dBm  
**Source principale** : Great Scott Gadgets, page produit officielle — https://greatscottgadgets.com/hackrf/one/  
**Source secondaire** : Great Scott Gadgets, ReadTheDocs officiel — https://hackrf.readthedocs.io/en/latest/hackrf_one.html  
**Citation** : "HackRF One is a wide band software defined radio (SDR) peripheral capable of transmission or reception of radio signals from 1 MHz to 6 GHz."  
**Confiance** : 0.97  
**Statut** : verified  
**Limites** : Émission soumise à licence dans la plupart des pays. Puissance TX variable par bande (5–15 dBm bas de bande, -10–0 dBm au-dessus de 4 GHz). Entrée max -5 dBm. Mode half-duplex uniquement.

---

### Entrée 2 — Wi-Fi 2.4 GHz

**Produit** : Wi-Fi 802.11b/g/n/ax — 2.4 GHz  
**Fabricant** : IEEE (standard)  
**Modèle exact** : IEEE 802.11b/g/n/ax, bande 2.4 GHz ISM  
**Catégorie** : Protocole sans fil  
**Pays/région** : Global (canaux autorisés variables)  
**Fréquence ou plage** : 2 401 MHz – 2 484 MHz  
**Unité normalisée** : 2 401 000 000 Hz – 2 484 000 000 Hz  
**Type de signal** : Wi-Fi  
**Protocole/modulation** : OFDM, DSSS, CCK — IEEE 802.11b/g/n/ax  
**Puissance** : EU: 100 mW EIRP (20 dBm) — USA: 1 W max (30 dBm)  
**Source principale** : IEEE 802.11-2020 Standard — https://standards.ieee.org/ieee/802.11/7028/  
**Source secondaire** : ETSI EN 300 328 V2.2.2 (EU) ; FCC Part 15.247 (USA)  
**Citation** : "2.4 GHz ISM band: 2.400–2.4835 GHz"  
**Confiance** : 0.98  
**Statut** : verified  
**Limites** : Canaux autorisés: 1–11 (USA), 1–13 (Europe), 1–14 (Japon). Canaux 12–14 interdits en France.

---

### Entrée 3 — Wi-Fi 5 GHz

**Produit** : Wi-Fi 802.11a/n/ac/ax — 5 GHz  
**Fabricant** : IEEE (standard)  
**Modèle exact** : IEEE 802.11a/n/ac/ax, bande 5 GHz  
**Catégorie** : Protocole sans fil  
**Pays/région** : Global (sous-bandes variables)  
**Fréquence ou plage** : 5 150 MHz – 5 850 MHz  
**Unité normalisée** : 5 150 000 000 Hz – 5 850 000 000 Hz  
**Type de signal** : Wi-Fi  
**Protocole/modulation** : OFDM — IEEE 802.11a/n/ac/ax  
**Puissance** : EU: 200 mW EIRP UNII-1 / 1 W EIRP UNII-2/3  
**Source principale** : IEEE 802.11-2020 — https://standards.ieee.org/ieee/802.11/7028/  
**Source secondaire** : ETSI EN 301 893 V2.1.1  
**Citation** : "5 GHz band: 5.150–5.850 GHz (varies by regulatory domain)"  
**Confiance** : 0.97  
**Statut** : verified  
**Limites** : DFS obligatoire sur UNII-2A (5.25–5.35 GHz) et UNII-2C (5.47–5.725 GHz). Sous-bandes et canaux autorisés varient selon pays.

---

### Entrée 4 — Wi-Fi 6E (6 GHz)

**Produit** : Wi-Fi 6E 802.11ax — 6 GHz  
**Fabricant** : IEEE / Wi-Fi Alliance (standard)  
**Modèle exact** : IEEE 802.11ax, bande 6 GHz  
**Catégorie** : Protocole sans fil  
**Pays/région** : Variable (USA: full band, Europe: partiel)  
**Fréquence ou plage** : 5 925 MHz – 7 125 MHz  
**Unité normalisée** : 5 925 000 000 Hz – 7 125 000 000 Hz  
**Type de signal** : Wi-Fi  
**Protocole/modulation** : OFDM — IEEE 802.11ax  
**Puissance** : EU: 23 dBm EIRP indoor (LPI), 14 dBm outdoor (VLP)  
**Source principale** : Wi-Fi Alliance — https://www.wi-fi.org/discover-wi-fi/wi-fi-6e  
**Source secondaire** : ECC Decision (21)03 — https://docdb.cept.org/download/3416  
**Citation** : "Wi-Fi 6E extends Wi-Fi into the 6 GHz band (5.925–7.125 GHz)"  
**Confiance** : 0.92  
**Statut** : partially_verified  
**Limites** : Europe: seulement 5.925–6.425 GHz autorisé. Vérifier décision nationale ANFR pour France. USA: bande complète autorisée FCC. Nombreux pays n'ont pas encore autorisé cette bande.

---

### Entrée 5 — Bluetooth / BLE

**Produit** : Bluetooth Classic + BLE 5.x  
**Fabricant** : Bluetooth SIG (standard)  
**Modèle exact** : Bluetooth 5.x (BR/EDR + LE)  
**Catégorie** : Protocole sans fil  
**Pays/région** : Global  
**Fréquence ou plage** : 2 402 MHz – 2 480 MHz  
**Unité normalisée** : 2 402 000 000 Hz – 2 480 000 000 Hz  
**Type de signal** : Bluetooth  
**Protocole/modulation** : GFSK / π/4-DQPSK / 8DPSK — Bluetooth 5.x  
**Puissance** : Classe 1: 20 dBm max ; Classe 2: 4 dBm ; Classe 3: 0 dBm  
**Source principale** : Bluetooth Core Specification 5.4 — https://www.bluetooth.com/specifications/specs/core-specification-5-4/  
**Source secondaire** : ETSI EN 300 328 V2.2.2  
**Citation** : "The RF channel frequencies used by Bluetooth are 2402 + k*1 MHz for k=0..78 (BR/EDR); BLE: 2402 + k*2 MHz for k=0..39"  
**Confiance** : 0.98  
**Statut** : verified  
**Limites** : Bluetooth Direction Finding (AoA/AoD) = traitement logiciel sur données IQ brutes. HackRF ne fournit PAS AoA nativement. FHSS sur 79 canaux (Classic). 3 canaux publicitaires BLE: 37 (2402 MHz), 38 (2426 MHz), 39 (2480 MHz).

---

### Entrée 6 — ISM 433 MHz (Europe)

**Produit** : Dispositifs 433 MHz ISM — Europe  
**Fabricant** : Various (télécommandes, capteurs IoT, domotique)  
**Modèle exact** : Bande ISM 433.050–434.790 MHz EU  
**Catégorie** : sub-GHz IoT  
**Pays/région** : Europe (ITU Region 1)  
**Fréquence ou plage** : 433 050 kHz – 434 790 kHz (centre: 433 920 kHz)  
**Unité normalisée** : 433 050 000 Hz – 434 790 000 Hz  
**Type de signal** : ISM sub-GHz  
**Protocole/modulation** : OOK / ASK / FSK (variable selon fabricant)  
**Puissance** : 10 mW e.r.p. (10 dBm) max EU  
**Source principale** : ETSI EN 300 220-1 V3.1.1 — https://www.etsi.org/deliver/etsi_en/300200_300299/30022001/03.01.01_60/en_30022001v030101p.pdf  
**Citation** : "Band 433.050–434.790 MHz: SRD applications, max e.r.p. 10 mW, duty cycle ≤ 10%"  
**Confiance** : 0.93  
**Statut** : partially_verified  
**Limites** : NON autorisé en USA sans vérification FCC. Duty cycle ≤ 10% en Europe. La fréquence seule ne permet pas d'identifier l'appareil ou le protocole.

---

### Entrée 7 — ISM 868 MHz (Europe)

**Produit** : Dispositifs 868 MHz ISM — Europe  
**Fabricant** : Various (LoRa, Z-Wave, Zigbee, domotique)  
**Modèle exact** : Bande 863–870 MHz EU  
**Catégorie** : sub-GHz IoT  
**Pays/région** : Europe  
**Fréquence ou plage** : 863 000 kHz – 870 000 kHz (centre: 868 000 kHz)  
**Unité normalisée** : 863 000 000 Hz – 870 000 000 Hz  
**Type de signal** : ISM sub-GHz  
**Protocole/modulation** : FSK / GFSK / LoRa (CSS) / OOK  
**Puissance** : 25 mW e.r.p. (14 dBm) sur 868–868.6 MHz  
**Source principale** : ETSI EN 300 220-2 V3.2.1 — https://www.etsi.org/deliver/etsi_en/300200_300299/30022002/03.02.01_60/en_30022002v030201p.pdf  
**Confiance** : 0.94  
**Statut** : partially_verified  
**Limites** : Cette bande N'EXISTE PAS aux USA. Duty cycle 1% sur 868–868.6 MHz. Différentes puissances selon sous-bande. LoRa EU868 utilise 868.1/868.3/868.5 MHz comme canaux publicitaires.

---

### Entrée 8 — ISM 915 MHz (USA/Amériques)

**Produit** : Dispositifs 915 MHz ISM — USA  
**Fabricant** : Various (LoRa, Zigbee sub-GHz, domotique USA)  
**Modèle exact** : Bande ISM 902–928 MHz USA  
**Catégorie** : sub-GHz IoT  
**Pays/région** : USA / Amériques  
**Fréquence ou plage** : 902 000 kHz – 928 000 kHz (centre: 915 000 kHz)  
**Unité normalisée** : 902 000 000 Hz – 928 000 000 Hz  
**Type de signal** : ISM sub-GHz  
**Protocole/modulation** : FSK / GFSK / LoRa (CSS) / FHSS  
**Puissance** : 1 W (30 dBm) max FCC Part 15.247  
**Source principale** : FCC Part 15.247 — https://www.ecfr.gov/current/title-47/chapter-I/subchapter-A/part-15/subpart-C/section-15.247  
**Citation** : "Operation in the 902–928 MHz band... output power not exceeding 1 watt"  
**Confiance** : 0.94  
**Statut** : partially_verified  
**Limites** : NON autorisé en Europe pour émission. FHSS souvent requis pour puissance maximale.

---

### Entrée 9 — 315 MHz télécommandes (USA)

**Produit** : Dispositifs 315 MHz FCC Part 15 — USA  
**Fabricant** : Various (télécommandes portes garage, alarmes)  
**Modèle exact** : 315 MHz center freq, USA  
**Catégorie** : sub-GHz télécommande  
**Pays/région** : USA  
**Fréquence ou plage** : 314 000 kHz – 316 000 kHz (centre: 315 000 kHz)  
**Unité normalisée** : 314 000 000 Hz – 316 000 000 Hz  
**Type de signal** : sub-GHz télécommande  
**Protocole/modulation** : OOK / ASK (propriétaire selon fabricant)  
**Puissance** : Très faible, < 0 dBm typique (champ max 6000 μV/m à 3m)  
**Source principale** : FCC Part 15.231 — https://www.ecfr.gov/current/title-47/chapter-I/subchapter-A/part-15/subpart-C/section-15.231  
**Confiance** : 0.80  
**Statut** : partially_verified  
**Limites** : NON autorisé en Europe pour émission. Protocole propriétaire selon fabricant. La fréquence seule ne suffit pas à identifier l'appareil.

---

## Fiches rejetées

| # | Sujet | Raison |
|---|-------|--------|
| R1 | Caméras sans fil — fréquences génériques | Impossible d'identifier une caméra par sa fréquence seule. Aucune référence produit + source officielle disponible pour une entrée générique. Règle: enregistrer seulement les bandes publiées par le fabricant. |
| R2 | Profil RF sans fréquence Hz | Test de validation: rejet automatique si aucune valeur de fréquence en Hz fournie |
| R3 | Source sans URL ni référence documentaire | Test de validation: rejet automatique si aucune URL ni doc_ref |

---

## Tests exécutés

| Test | Résultat | Détail |
|------|----------|--------|
| creation_db | PASS | Tables créées sans erreur |
| insertion_produit | PASS | 9 produits insérés |
| insertion_profil_rf | PASS | 9 profils RF insérés |
| insertion_source_officielle | PASS | 10 sources high-reliability |
| rejet_fiche_sans_source | PASS | Source sans URL ni doc_ref → rejetée |
| rejet_frequence_sans_unite | PASS | Profil sans fréquence Hz → rejeté |
| validation_juridiction | PASS | 10 entrées avec juridiction |
| detection_doublons | PASS | IntegrityError sur insertion doublon |
| statut_conflicting | PASS | Statut conflicting inséré correctement |
| export_csv | PASS | rf_export.csv généré (7157 bytes) |
| export_json | PASS | rf_export.json généré (24594 bytes) |
| absence_secrets | PASS | Aucun secret dans le code |
| absence_commande_hackrf | PASS | Aucune commande hackrf_* exécutée |

**Score : 13/13 PASS**

---

## Informations restant à vérifier

1. **HackRF One TX power par bande** : valeurs variables selon la documentation readthedocs (5–15 dBm bas de bande, -10–0 dBm > 4 GHz). À confirmer avec datasheet composants RF (MAX5865/RFFC5072).
2. **Wi-Fi HaLow 802.11ah** (bande 900 MHz) : non documenté, source IEEE requise.
3. **LoRaWAN EU868 canaux exacts** : à vérifier avec LoRa Alliance Regional Parameters v2.1.
4. **Z-Wave EU 868.42 MHz** : confirmation sur déclaration CE d'un produit spécifique.
5. **Wi-Fi 6E France** : vérifier décision ANFR d'application nationale de ECC (21)03.
6. **315 MHz hors USA** : réglementation dans d'autres pays non vérifiée.
7. **Caméras sans fil propriétaires** : aucune entrée possible sans référence produit + source officielle.

---

## Sécurité — Confirmations

- ✅ Aucun accès au HackRF réel
- ✅ Aucune émission radio
- ✅ Aucune commande radio exécutée
- ✅ Aucun brouillage, rejeu, injection ou interception
- ✅ Aucune fréquence présentée comme légalement utilisable sans précision de pays
- ✅ Aucun secret, mot de passe, clé API dans les fichiers
- ✅ Uniquement des informations techniques publiques de sources officielles
