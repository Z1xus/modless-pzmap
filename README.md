# modless-pzmap

live position for [projectzomboidmap.com](https://projectzomboidmap.com/) with no mods.

this is possible because the game exposes player data under stable method names (getX, getY). originally the mod writes a live.txt under the zomboid folder with your position, this does exactly the same, except instead of the mod, we load a tiny .jar helper into the game

> [!NOTE]
> if you play on servers this may be bannable. use at your own risk

## usage
1. download `pzm_live.exe` from [releases](https://github.com/z1xus/modless-pzmap/releases/latest)
2. run it, then start the game
3. load a save and pick the zomboid folder on the site

`pzm_live.exe` is essentially an installer. the game loads the embedded jar when it starts, and it handles all live updates. the installer exits after setup and does not need to stay open while you play. it is a [cross-platform ape binary](https://github.com/jart/cosmopolitan), meaning it works on windows, linux, and mac.  
to uninstall, close the game and run `pzm_live.exe --uninstall`.

linux path: `~/.var/app/com.valvesoftware.Steam/Zomboid` or `~/Zomboid`  
windows path: `%USERPROFILE%/Zomboid`

## build
needs a jdk 25 and a c++23 compiler: `sh tools/build.sh`
