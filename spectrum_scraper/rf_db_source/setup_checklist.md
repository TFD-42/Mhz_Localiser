# HackRF One — Checklist de préparation (SANS utilisation du matériel)

> **Ces étapes doivent être exécutées manuellement par l'utilisateur après avoir quitté cette session.**
> Aucune commande ci-dessous n'a été exécutée automatiquement.

## 1. Installation du logiciel officiel HackRF

- [ ] Télécharger le logiciel officiel depuis le dépôt Great Scott Gadgets :
  - Source officielle : https://github.com/greatscottgadgets/hackrf
  - Documentation : https://hackrf.readthedocs.io/en/latest/installing_hackrf_software.html
- [ ] Sur macOS : `brew install hackrf` (via Homebrew)
- [ ] Sur Linux (Debian/Ubuntu) : `sudo apt install hackrf`
- [ ] Sur Windows : utiliser le package binaire officiel fourni par GSG

## 2. Vérification des pilotes

- [ ] Vérifier que les pilotes USB sont correctement installés
- [ ] Sur Linux : vérifier les règles udev (`/etc/udev/rules.d/53-hackrf.rules`)
- [ ] Sur Windows : installer le pilote WinUSB via Zadig (https://zadig.akeo.ie/)
- [ ] Ne brancher le HackRF qu'après installation des pilotes

## 3. Vérification du firmware

- [ ] Vérifier la version du firmware installé sur le HackRF
- [ ] Comparer avec la version officielle disponible sur GitHub Releases
- [ ] Ne mettre à jour le firmware que depuis les sources officielles Great Scott Gadgets

## 4. Premier test matériel — UNIQUEMENT après installation manuelle

- [ ] Brancher le HackRF via USB
- [ ] Exécuter `hackrf_info` pour vérifier la détection (commande de lecture uniquement)
- [ ] Vérifier la présence d'un numéro de série valide dans la sortie

## 5. Vérification du numéro de série

- [ ] Le numéro de série doit apparaître dans la sortie de `hackrf_info`
- [ ] Conserver ce numéro pour identifier l'appareil

## 6. Vérification de l'antenne

- [ ] Utiliser une antenne adaptée à la bande de réception visée
- [ ] Connecteur : SMA femelle
- [ ] Ne pas laisser le port antenne sans charge lors d'une émission
- [ ] Vérifier l'accord d'antenne si une bande précise est ciblée

## 7. Atténuateur (signal fort)

- [ ] Si un signal fort est attendu (émetteur proche, signal de puissance), utiliser un atténuateur externe
- [ ] Puissance d'entrée maximale HackRF : **-5 dBm**
- [ ] Dépasser -5 dBm en entrée peut causer des dommages permanents au matériel

## 8. Démarrer en réception uniquement

- [ ] Commencer par des opérations de réception (RX) uniquement
- [ ] Ne jamais lancer une commande TX sans avoir vérifié les obligations réglementaires

## 9. Ne jamais commencer par une commande d'émission

- [ ] `hackrf_transfer -t` (transmit) ne doit PAS être la première commande
- [ ] Vérifier d'abord `hackrf_info`, puis la réception (`hackrf_transfer -r`)
- [ ] Toute émission nécessite une vérification réglementaire préalable

## 10. Vérification des obligations réglementaires du pays

- [ ] Identifier la juridiction applicable (France, USA, autre)
- [ ] **France** : consulter ARCEP (https://www.arcep.fr/) et ANFR (https://www.anfr.fr/)
- [ ] **Europe** : consulter les normes ETSI et les décisions CEPT
- [ ] **USA** : consulter les règles FCC Part 15 et Part 97 (si radioamateur)
- [ ] L'émission sans autorisation est une infraction dans la plupart des pays
- [ ] Les licences radioamateurs permettent l'émission sur certaines bandes uniquement

---

> **Rappel de sécurité** : Le HackRF One est un outil de recherche et de développement SDR.
> Son utilisation pour émettre sur des fréquences sans autorisation est illégale.
> La réception passive est généralement légale mais vérifier la législation locale.
> Ne jamais émettre, brouiller, rejouer ou intercepter des communications protégées.
