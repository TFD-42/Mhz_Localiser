# Contributing to Geo-Flip

Thanks for your interest in contributing!

## Ways to contribute

- **Bug reports** — open an issue with steps to reproduce, device info, and Flipper firmware version
- **Feature requests** — open an issue describing the use case
- **Pull requests** — welcome for bug fixes, new FAP features, propagation model improvements, UI polish

## Development setup

### Flipper FAP
```bash
git clone https://github.com/flipperdevices/flipperzero-firmware
cp -r Build/rf_logger flipperzero-firmware/applications_user/
cd flipperzero-firmware && ./fbt fap_rf_logger
```

### Android app
```bash
npm install
npx cap sync android
cd android && ./gradlew assembleDebug
```

## Pull request guidelines

1. Fork the repo and create a branch from `main`
2. Keep PRs focused — one feature or fix per PR
3. Test on real hardware if possible (Flipper + Android)
4. Update the README if behaviour changes
5. PRs are merged via **squash commit** — keep commit history clean

## Code style

- C (Flipper): follow existing style, no dynamic allocation in hot paths
- Java (Android plugin): standard Android conventions
- JS/HTML/CSS: no frameworks beyond Leaflet + Capacitor

## Reporting security issues

Please **do not** open public issues for security vulnerabilities. See [SECURITY.md](SECURITY.md).
