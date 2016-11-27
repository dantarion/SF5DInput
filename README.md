# SF5DInput
DirectInput support for Street Fighter 5 PC

This is a drop in DLL replacement for *xinput1_3.dll* that provides DirectInput support for Street Fighter 5 PC. It focuses on having direct bindings for PS3/PS4 controllers that match up with default bindings that would be used on the PS4 version of the game. It also provides features for swapping which slots can be assigned to each controller.

## How to Use?

Download the latest version of the dll in the [release section](https://github.com/dantarion/SF5DInput/releases).
Drop the xinput1_3.dll file into *installdirectory\StreetFighterV\Binaries\Win64\* and launch the game. To uninstall, just remove the dll file from this folder.

## Features

1. Supports DirectInput as well as XInput devices (analog input supported)
2. You can plug or unplug your devices while the game is running
3. Devices are automatically assigned (first plugged, first player assigned, places are kept).
4. By holding the Home Button and left or right on the DPad for 2 seconds, you can switch between being player 1 or player 2 on any controller (on XInput controller Start+Back can be used if it's not working).

## Tournament mode

This version of the dll disable controller switch to avoid having a player claiming a slot during a match. The slot allocation and hotplug still works (first plugged, first player assigned).

## Known Bugs

1. Controllers that aren't PS3/PS4 controllers don't have a way to get proper mapping :(

## Tested with....

1. Hori FC4
2. Sony DualShock 4
3. Hori PS4 VLX
4. PS360+ in any mode
5. Venom PS4
6. Madcatz TE2
7. Hori RAP 4
8. Madcatz TE

## Credits

* @dantarion
* @WydD