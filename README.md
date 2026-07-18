# gimgtools

Garmin Image Tools — a set of command-line tools to examine and manipulate Garmin IMG (map format) files.

## Building

### Linux / macOS

```bash
cmake -B build
cmake --build build
```

### Windows (Visual Studio)

```cmd
cmake -B build -G "Visual Studio 18 2026"
cmake --build build --config Release
```

All executables are placed in the `build/` directory.

### Requirements

- CMake 3.0 or later
- C compiler (GCC, Clang, or MSVC)

## Tools

### User Tools

| Tool | Description |
|------|-------------|
| `gimgunlock` | Unlock a locked map so it can be used on ALL devices. Works by decrypting the TRE sections. |
| `gimgxor` | Unscramble maps that have been XOR-obfuscated. |
| `gimgchcodepage` | Change the codepage in LBL subfiles from 65001 (UTF-8) to 1252 (Latin-1). Needed for devices like GPSMAP 64 that reject Unicode-encoded maps. |

### Reverse Engineering Tools

| Tool | Description |
|------|-------------|
| `gimginfo` | Print information about a map. |
| `gimgextract` | Extract IMG sections. |
| `gimgch` | Hexdump and compare section headers of two or more IMG files. |
| `gimgfixcmd` | Fix China map coordinate deviation. |
| `cmdc` | Generate deviation table. |

## Usage

### Basic Workflow

1. If your map is XOR-obfuscated:
   ```bash
   gimgxor map.img output.img
   ```

2. If your map is locked:
   ```bash
   gimgunlock map.img
   ```

3. If your device rejects the map:
   ```bash
   gimgchcodepage map.img
   ```

4. Verify with:
   ```bash
   gimginfo map.img
   ```

### Tool Usage

```
gimgunlock map.img
gimgxor infile.img outfile.img
gimgchcodepage [-v] map.img
gimginfo map.img [subfile]
gimgextract map.img
gimgch [-w columns] [-m max_sf_per_img] [-s pattern] map1.img map2.img ...
```

## China Map Coordinates

Research on the deviation of China map coordinates is documented in this article:
[China Map Deviation as a Regression Problem](http://wuyongzheng.wordpress.com/2010/01/22/china-map-deviation-as-a-regression-problem/)

The goal is to change coordinates in place (individual bytes without recompiling the map). The `cmdc` tool generates the deviation table, and `gimgfixcmd` applies the correction.

## Bug Reports

Please report bugs on [GitHub](https://github.com/wuyongzheng/gimgtools/issues).

## Credits

Most of the reverse engineering on the IMG format was done by others:

- [Garmin IMG Format](http://sourceforge.net/projects/garmin-img/)
- [Libgarmin](http://libgarmin.sourceforge.net/)
- [ati.land.cz](http://ati.land.cz/)
- [NOD Subfile Format](http://svn.parabola.me.uk/display/trunk/doc/nod.txt)
- [MDR Subfile Format](http://wiki.openstreetmap.org/wiki/OSM_Map_On_Garmin/MDR_Subfile_Format)

The reverse engineering of the unlocking algorithm and the GMP section (NT format by Garmin) was done by the author of this toolkit.
