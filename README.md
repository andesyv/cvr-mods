# Sticky Menu

A small ChilloutVR mod that makes the menu follow your view. It also enables you to grab the menu as a prop and position
the menu around in space. Uses [BepInEx](https://github.com/BepInEx/BepInEx ).

## Installing

Just add the dll file to the mods folder.

## Dragging

You can drag the menu by grabbing the sides of the menu like you would grab a prop, or by grabbing it with the offhand
you are not currently using to navigate the menu (interact with the menu to switch hand).

Originally I wanted you to be able to grab the menu anywhere there aren't clickable buttons, and the logic for this is
in the mod, but the way I've implemented this is using a JavaScript event that needs to be fired from the menu. And I
did not find a way to inject JavaScript code into the menu from a mod, yet. You can enable this feature yourself by
adding a JavaScript event that triggers the `CVRNoButtonClicked` engine event and setting `UseEdgeDragging` to false.

## Config

The default config file is located at
`<game directory>/BepInEx/config/dev.syvertsen.plugins.stickymenu.cfg`.

There's descriptions for the different configurations in the config file, but they are described here aswell:
| Name                  | Default   | Explanation           |
| --------------------- | --------- | --------------------- |
| Enabled               | `true`    | Enable/disable mod    |
| LockMenuPosition      | `true`    | Whether to lock the menus position to the player |
| LockMenuRotation      | `true`    | Whether to lock the menus rotation to the player |
| EnableMenuDragging    | `true`    | Whether to enable dragging the menu by grabbing it |
| UseEdgeDragging       | `true`    | Whether to drag by grabbing the edge. The alternative is using the JavaScript event. See [Dragging](##dragging).     |

## Disclaimer

This modification is unofficial and not supported by Alpha Blend Interactive. Using this modification might cause issues
with performance, security or stability of the games.

I too am in no way affiliated with or supported by Alpha Blend Interactive and I take no responsibilities for any harm
done by this mod. Use at your own risk.

## Changed original code

The `StickyMenu.MethodPatcher` class is responsible for all method modifications:

 - `ViewManager.UiStateToggle` has been changed to add an event on menu enable/disable
 - `ControllerRay.Update` has had it's logic reworked to enable overriding of menu interaction and add events on clicking outside / inside of menu
 - `ControllerRay.GrabObject` has been changed to add an event on menu grabbing

## Licence

MIT