
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
            var handler = show ? MethodPatcher.OnMenuEnabled : MethodPatcher.OnMenuDisabled;
            if (handler != null)
                handler();
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

        public static void Prefix(ControllerRay __instance)
        {
            MethodPatcher.rayInstance = __instance;
            var MenuHit = !(ViewManager.Instance.uiCollider is null) && ViewManager.Instance.uiCollider.Raycast(
                new Ray(__instance.transform.position, __instance.transform.TransformDirection(__instance.RayDirection)), out MethodPatcher.hitInfo,
                1000f);
            var FocusAwayFromMenu = LastMenuHit && !MenuHit;
            LastMenuHit = MenuHit;

            /// If dragging, raytracing will always fail, so just go by the input instead.
            var down = StickyMenuMod.Dragging ? MouseDown(__instance) : (MenuHit && MouseDown(__instance));
            var up = StickyMenuMod.Dragging ? MouseUp(__instance) : (!MenuHit || MouseUp(__instance));

            var pressed = !LastMouseDown && down;
            var released = !down && !LastMouseUp && up;


            LastMouseDown = down;
            LastMouseUp = up;

            if (pressed)
            {
                MethodPatcher.MouseDownOnMenu = true;
                var handler = MethodPatcher.OnMenuMouseDown;
                if (handler != null)
                    handler();
            }

            if (released)
            {
                MethodPatcher.MouseDownOnMenu = false;
                var handler = MethodPatcher.OnMenuMouseUp;
                if (handler != null)
                    handler();
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
