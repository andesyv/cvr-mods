# Sticky Menu

A small ChilloutVR mod that makes the main menu follow your view. It also enables you to grab the main menu as a prop and position
it around in space.

**Note:** Currently the rotation is a bit wonky and I also have not confirmed that it works on the current version of the game so stuff might be broken.

## Dragging

You can drag the main menu by grabbing the sides of the menu like you would grab a prop, or by grabbing it with the offhand
you are not currently using to navigate the menu (interact with the menu to switch hand).

Originally I wanted you to be able to grab the menu anywhere there aren't clickable buttons, and the logic for this is
in the mod, but the way I've implemented this is using a JavaScript event that needs to be fired from the menu. And I
did not find a way to inject JavaScript code into the menu from a mod, yet. You can enable this feature yourself by
adding a JavaScript event that triggers the `CVRNoButtonClicked` engine event and setting `UseEdgeDragging` to false.

## Config

The default config file is located at
`<game directory>/BepInEx/config/com.andough.stickymenu.cfg`.

There's descriptions for the different configurations in the config file, but they are described here aswell:
| Name                  | Default   | Explanation           |
| --------------------- | --------- | --------------------- |
| Enabled               | `true`    | Enable/disable mod    |
| LockMenuPosition      | `true`    | Whether to lock the menus position to the player |
| LockMenuRotation      | `true`    | Whether to lock the menus rotation to the player |
| EnableMenuDragging    | `true`    | Whether to enable dragging the menu by grabbing it |
| UseEdgeDragging       | `true`    | Whether to drag by grabbing the edge. The alternative is using the JavaScript event. See [Dragging](##dragging).     |

## Patched original code

The `StickyMenu.MethodPatcher` class is responsible for all method modifications:

 - `ViewManager.UiStateToggle` has been changed to add an event on menu enable/disable
 - `ControllerRay.Update` has had it's logic reworked to enable overriding of menu interaction and add events on clicking outside / inside of menu
 - `ControllerRay.GrabObject` has been changed to add an event on menu grabbing
