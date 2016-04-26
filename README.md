# SF5DInput
DirectInput support for Street Fighter 5 PC

This is a drop in DLL replacement for *xinput1_3.dll* that provides DirectInput support for Street Fighter 5 PC. It focuses on having direct bindings for PS3/PS4 controllers that match up with default bindings that would be used on the PS4 version of the game. It also provides features for swapping which slots can be assigned to each controller.

## How to Use?

Drop all included files into *installdirectory\StreetFighterV\Binaries\Win64\* and launch the game. To uninstall, just remove the dll files from this folder.

## Features (as of alpha release)

1. A single DirectInput controller will work, alongside XInput controllers.
2. By holding the Home Button and left or right on the DPad, you can switch between being player 1 or player 2 on the DirectInput controller.

## Known Bugs

1. This currently only supports a single DirectInput controller
2. The DirectInput controller cannot be unplugged and replugged without restarting the game
3. Doesn't work with controllers that use the Analog Joystick, only controllers that use the DPad/Hatswitch
4. Controllers that aren't PS3/PS4 controllers don't have a way to get proper mapping :(

## Tested with....

1. Hori FC4
2. Sony DualShock 4
3. Hori PS4 VLX

## Credits

* @dantarion
