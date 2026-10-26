# ares: the Mega EverDrive PRO board

| File | What |
|---|---|
| `everdrive-pro-core-v148.patch` | **the core, as of 25 Oct 2026**, against upstream ares v148: the board (`everdrive-pro.cpp`), the RAM-cart board, the 32X wrapper with the console's rules (`mega-32x.cpp`), the 68000 bus with the disturbance hook and the two-drivers read (`bus/inline.hpp`), the cartridge hooks, mia's board choice. `git clone --branch v148 https://github.com/ares-emulator/ares && git apply everdrive-pro-core-v148.patch`. It gives the core only: a front end still has to put a cart (and a launcher) in the slot while a disc boots - `README-CD32X.md` and the CD32X BootCamp's docs/11 §5 say how, and the ares CD32X build does it (desktop and headless). |
| `everdrive-pro-ares-v148-first.patch` | the first version of the board (24 Oct 2026), kept for the record: the mapper as `edtest` proved it, before the cartridge boot's rules |
| `everdrive-pro.cpp`, `mega-32x.cpp`, `inline.hpp` | the three files as they are now, for reading |
| `README-CD32X.md` | the ares CD32X build's manual (Windows, Android, headless, every switch) |
| `RELEASE_NOTES_v1.1.md` | what each release changed |

Licence: the patches change ares and are under ares's ISC licence (https://github.com/ares-emulator/ares).
