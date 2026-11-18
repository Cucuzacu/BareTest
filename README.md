# BareTest

**BareTest** is a lightweight port of Minetest 0.3.0 optimized to run on bootleg handhelds running embedded Linux. Feel free to write port requests for your console in the issues tab!
It uses a software renderer so it can even run on consoles without a gpu.
It uses less than 32 mb of ram.

---

## Controls & Key Mapping on the v90

All controls are fully configurable in `minetest.conf`.

Look around/move mouse: a,b,x,y;
Move forward: up arrow;
Left/right click: l1, r1;
Escape: select;
Jump: start;
Cycle hotbar 1 slot to the right: right arrow;
Open inventory: left arrow;
Go down ladders: down arrow;
Crouch: circular button that you hold to shut down the console.

## Building from Source

For miyoo cfw:

```bash
./build-miyoo.sh
```

To clean build artifacts:

```bash
./build.sh clean
```

Or building for windows:

```bash
build-irrlicht.bat
build.bat
```

Then copy the resulting .exe and .dll to windows/bin.

---

## Installation

1. Copy the output folder(baretest) directory to your sdcard.
2. Launch BareTest from the file explorer.

---

## License

- **BareTest**: Ported by me(Cucuzacu).
- **Minetest-c55**: Copyright (C) 2010-2011 Perttu Ahola (celeron55).
- Licensed under GPL-2.0+. See source files + `old-README.txt` for complete licensing information.
