# nymea-experience-plugin-energy

This repository contains the nymea experience plugin that exposes energy-related
functionality to nymea. It provides the manager implementation, JSON-RPC APIs
and helper utilities used by the nymea runtime to collect, log and expose energy
data.

Alongside the plugin, the repository ships the reusable `libnymea-energy`
library that provides the abstract energy manager interfaces and helper
types for external plugins.

## Contents

- `plugin/`: the experience plugin (`nymea_experiencepluginenergy`) including JSON-RPC handler and energy manager implementation
- `libnymea-energy/`: reusable library providing core interfaces/types (`EnergyManager`, `EnergyLogs`, `EnergyPlugin`) for external energy plugins
- `debian-qt5/`, `debian-qt6/`: Debian packaging (the `debian` symlink selects the active one)

## Build

Build requirements:

- Qt 5 or Qt 6 (including `qmake`/`qmake6`)
- nymea development files (`pkg-config --cflags --libs nymea`)

Out-of-tree builds are recommended:

```sh
mkdir -p build
cd build
qmake ..     # Qt5
make -j
```

For Qt6:

```sh
mkdir -p build
cd build
qmake6 ..
make -j
```

## Install

```sh
cd build
make install
```

This installs:

- the experience plugin to `QT_INSTALL_LIBS/nymea/experiences/`
- `libnymea-energy` to `QT_INSTALL_LIBS` and headers to `QT_INSTALL_PREFIX/include/nymea-energy/`

Depending on your prefix, installation may require elevated permissions.

## Runtime notes

- Energy plugins are loaded at startup by scanning for shared objects named like `libnymea_energyplugin*.so`.
- Plugin discovery paths can be overridden using `NYMEA_ENERGY_PLUGINS_PATH` (replace defaults) and `NYMEA_ENERGY_PLUGINS_EXTRA_PATH` (prepend extra search directories). Both accept a colon-separated list of paths.
- Runtime state is persisted in `energy.conf` under `NymeaSettings::settingsPath()`.
- Logging uses the `EnergyExperience` category (e.g. enable debug logs via `QT_LOGGING_RULES="EnergyExperience.debug=true"`).

## Translations

- Update `.ts` files: `make lupdate` (from the build directory)
- Build `.qm` files: `make lrelease`

## License

The nymea experience plugin sources are licensed under the terms of the
GNU General Public License version 3 or (at your option) any later
version. The shared `libnymea-energy` library is licensed under the GNU
Lesser General Public License version 3 or (at your option) any later
version. Refer to the included `LICENSE.GPL3`, `LICENSE.LGPL3` and the
SPDX headers inside the source files for the complete text.
