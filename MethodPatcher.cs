
using System;
using ABI_RC.Core.InteractionSystem;
using HarmonyLib;

namespace StickyMenu
{
    class MethodPatcher
    {
        public static Action<ViewManager> OnMenuEnabled;
        public static Action<ViewManager> OnMenuDisabled;

        public static void DoPatching()
        {
            var harmony = new HarmonyLib.Harmony("andough.stickymenu.patch");
            harmony.PatchAll();
        }
    }

    [HarmonyPatch(typeof(ViewManager), "UiStateToggle", new Type[] { typeof(bool) })]
    public class ViewManagerPatch
    {
        /*private static AccessTools.FieldRef<ViewManager, bool> _gameMenuOpenRef =
            AccessTools.FieldRefAccess<ViewManager, bool>("_gameMenuOpen");*/

        public static void Postfix(bool show)
        {
            if (show)
                MethodPatcher.OnMenuEnabled(__instance);
            else
                MethodPatcher.OnMenuDisabled(__instance);
        }
    }
}
