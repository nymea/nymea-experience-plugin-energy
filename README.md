# nymea-experience-plugin-energy

This repository contains the nymea experience plugin that exposes
energy-related functionality to nymea. It provides the manager
implementation, JSON APIs and helper utilities used by the nymea
runtime to collect, log and expose energy data.

Alongside the plugin, the repository ships the reusable `libnymea-energy`
library that provides the abstract energy manager interfaces and helper
types for external plugins.

## License

The nymea experience plugin sources are licensed under the terms of the
GNU General Public License version 3 or (at your option) any later
version. The shared `libnymea-energy` library is licensed under the GNU
Lesser General Public License version 3 or (at your option) any later
version. Refer to the included `LICENSE.GPL3`, `LICENSE.LGPL3` and the
SPDX headers inside the source files for the complete text.
