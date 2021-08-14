
using System;
using ABI_RC.Core.InteractionSystem;
using HarmonyLib;

namespace StickyMenu
{
    class MethodPatcher
    {
        public static Action OnMenuEnabled;
        public static Action OnMenuDisabled;

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
}
