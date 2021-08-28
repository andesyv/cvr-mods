
using System;
using System.Reflection;
using ABI.CCK.Components;
using ABI_RC.Core.InteractionSystem;
using ABI_RC.Core.Savior;
using HarmonyLib;
using UnityEngine;

namespace StickyMenu
{
    internal class MethodPatcher
    {
        public static Action OnMenuEnabled;
        public static Action OnMenuDisabled;
        public static Action OnMenuMouseUp;
        public static bool MouseDownOnMenu = false;
        public static ControllerRay RayInstance = null;
        public static RaycastHit HitInfo = new RaycastHit();
        public static MethodInfo GrabObjectMethod;

        public static void DoPatching()
        {
            var harmony = new HarmonyLib.Harmony("andough.stickymenu.patch");
            harmony.PatchAll();

            GrabObjectMethod = typeof(ControllerRay).GetMethod("GrabObject", BindingFlags.Instance | BindingFlags.NonPublic, null, new[] { typeof(CVRPickupObject), typeof(RaycastHit)
            }, null);
        }
    }

    [HarmonyPatch(typeof(ViewManager), "UiStateToggle", typeof(bool))]
    public class ViewManagerPatch
    {
        public static void Postfix(bool show)
        {
            var handler = show ? MethodPatcher.OnMenuEnabled : MethodPatcher.OnMenuDisabled;
            handler?.Invoke();
        }
    }

    [HarmonyPatch(typeof(ControllerRay), "Update")]
    public class ControllerRayPatch
    {
        private static bool _lastMenuHit = false;
        private static bool _lastMouseDown = false;
        private static bool _lastMouseUp = false;

        private static readonly AccessTools.FieldRef<ControllerRay, bool> HandRef =
            AccessTools.FieldRefAccess<ControllerRay, bool>("hand");

        public static void Prefix(ControllerRay __instance)
        {
            MethodPatcher.RayInstance = __instance;
            var menuHit = !(ViewManager.Instance.uiCollider is null) && ViewManager.Instance.uiCollider.Raycast(
                new Ray(__instance.transform.position, __instance.transform.TransformDirection(__instance.RayDirection)), out MethodPatcher.HitInfo,
                1000f);
            _lastMenuHit = menuHit;

            // If dragging, ray tracing will always fail, so just go by the input instead.
            var down = StickyMenuMod.Dragging ? MouseDown(__instance) : (menuHit && MouseDown(__instance));
            var up = StickyMenuMod.Dragging ? MouseUp(__instance) : (!menuHit || MouseUp(__instance));

            var pressed = !_lastMouseDown && down;
            var released = !down && !_lastMouseUp && up;


            _lastMouseDown = down;
            _lastMouseUp = up;

            if (pressed)
            {
                MethodPatcher.MouseDownOnMenu = true;
            }

            if (released)
            {
                MethodPatcher.MouseDownOnMenu = false;
                var handler = MethodPatcher.OnMenuMouseUp;
                handler?.Invoke();
            }
        }

        private static bool MouseDown(ControllerRay __instance)
        {
            var hand = HandRef(__instance);
            return (hand ? CVRInputManager.Instance.interactLeftDown : CVRInputManager.Instance.interactRightDown) ||
                   (hand ? CVRInputManager.Instance.interactLeftValue : CVRInputManager.Instance.interactRightValue) > 0.800000011920929 ||
                   (hand ? CVRInputManager.Instance.gripLeftDown : CVRInputManager.Instance.gripRightDown);
        }

        private static bool MouseUp(ControllerRay __instance)
        {
            var hand = HandRef(__instance);
            return (hand ? CVRInputManager.Instance.interactLeftUp : CVRInputManager.Instance.interactRightUp) ||
                   (hand ? CVRInputManager.Instance.gripLeftUp : CVRInputManager.Instance.gripRightUp);
        }
    }
}
