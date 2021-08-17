
using System;
using ABI_RC.Core.InteractionSystem;
using ABI_RC.Core.Savior;
using HarmonyLib;
using UnityEngine;

namespace StickyMenu
{
    class MethodPatcher
    {
        public static Action OnMenuEnabled;
        public static Action OnMenuDisabled;
        public static Action OnMenuMouseDown;
        public static Action OnMenuMouseUp;
        public static bool MouseDownOnMenu = false;
        public static ControllerRay rayInstance = null;
        public static RaycastHit hitInfo = new RaycastHit();

        public static void DoPatching()
        {
            var harmony = new HarmonyLib.Harmony("andough.stickymenu.patch");
            harmony.PatchAll();
        }
    }

    [HarmonyPatch(typeof(ViewManager), "UiStateToggle", new Type[] { typeof(bool) })]
    public class ViewManagerPatch
    {
        public static void Postfix(bool show)
        {
            if (show)
                MethodPatcher.OnMenuEnabled();
            else
                MethodPatcher.OnMenuDisabled();
        }
    }

    [HarmonyPatch(typeof(ControllerRay), "Update")]
    public class ControllerRayPatch
    {
        private static bool LastMenuHit = false;
        private static bool LastMouseDown = false;
        private static bool LastMouseUp = false;

        private static AccessTools.FieldRef<ControllerRay, bool> handRef =
            AccessTools.FieldRefAccess<ControllerRay, bool>("hand");

        public static void Postfix(ControllerRay __instance)
        {
            MethodPatcher.rayInstance = __instance;
            var MenuHit = !(ViewManager.Instance.uiCollider is null) && ViewManager.Instance.uiCollider.Raycast(
                new Ray(__instance.transform.position, __instance.transform.TransformDirection(__instance.RayDirection)), out MethodPatcher.hitInfo,
                1000f);
            var FocusAwayFromMenu = LastMenuHit && !MenuHit;
            LastMenuHit = MenuHit;

            var down = MenuHit && MouseDown(__instance);
            var up = !MenuHit || MouseUp(__instance);

            var pressed = !LastMouseDown && down;
            var released = LastMouseDown && !LastMouseUp && up;

            LastMouseDown = down;
            LastMouseUp = up;

            if (pressed)
            {
                MethodPatcher.MouseDownOnMenu = true;
                MethodPatcher.OnMenuMouseDown();
            }

            if (released)
            {
                MethodPatcher.MouseDownOnMenu = false;
                MethodPatcher.OnMenuMouseUp();
            }
        }

        private static bool MouseDown(ControllerRay __instance)
        {
            var hand = handRef(__instance);
            return (hand ? CVRInputManager.Instance.interactLeftDown : CVRInputManager.Instance.interactRightDown) ||
                   ((double)(hand ? CVRInputManager.Instance.interactLeftValue : CVRInputManager.Instance.interactRightValue) > 0.800000011920929) ||
                   (hand ? CVRInputManager.Instance.gripLeftDown : CVRInputManager.Instance.gripRightDown);
        }

        private static bool MouseUp(ControllerRay __instance)
        {
            var hand = handRef(__instance);
            return (hand ? CVRInputManager.Instance.interactLeftUp : CVRInputManager.Instance.interactRightUp) ||
                   (hand ? CVRInputManager.Instance.gripLeftUp : CVRInputManager.Instance.gripRightUp);
        }
    }
}
