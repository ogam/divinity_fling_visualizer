## Divinity Fling Visualizer
Fairly accurate in game mapping tool for larian games.  
Currently supports dos1, dos2 and bg3.

## Demo
[![demo](http://img.youtube.com/vi/GE9lOBiWCAo/0.jpg)](https://www.youtube.com/watch?v=GE9lOBiWCAo)

## Routing
This tool allows you to plan out your flinging journey for these games.  
Scan tool will allow you to use your current camera persepctive to see where you can fling towards.  
Map tool will let you see where you've scanned so far or to see where you're currently trying to fling to.  
The map is fairly accurate to the in game world so you can use this to plan out node flings between level transitions.  

## Add new game
To add a new game add a new signature block to `game_signatures.txt` or address block to `game_addresses.txt`
- Make sure to include the `process` name as the game's binary file `EoCApp.exe` for dos1 and dos2 as an example.
- `game` field is whatever you want to call your game, this will be used to reference your game's meta data in `data/GAME` path
- Add any new level folders here the name will be the same one you see in the map tab, example `Cyseal` in dos1 will have a `Cyseal` tab in dos1
- `interests.txt` and `regions.txt` will be needed to be created, these can be empty
- Start up the visualizer tool and the game
- In `map` you can select your level, click on auto map and you can select your image on the bottom left of or copy nad paste your image from your clipboard, to auto map you'll need 2 reference points from the game to place this roughly in the tool otherwise the positioning will be off.
- Once you've placed the new region or interest to the level click export to save the meta data

## Build
Currently only setup with MSVC, run build.bat and you should be all set.  
If it complains about not finding vcvars.bat then add your MSVC path to the list in build.bat.  

## Libraries
Uses Minicoro (https://github.com/edubart/minicoro)  
Raylib (https://github.com/raysan5/raylib)  
Dear Imgui (https://github.com/ocornut/imgui)  
