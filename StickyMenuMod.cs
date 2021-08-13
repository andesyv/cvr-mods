using System;
using cohtml;
using MelonLoader;

namespace StickyMenu
{
    public class StickyMenuMod : MelonMod
    {
        enum Status
        {
            NotStarted,
            WaitingForEventRegistration,
            Finished
        }

        private Status initStatus = Status.NotStarted;
        private CohtmlView menuView;

        public override void OnSceneWasLoaded(int buildIndex, string sceneName)
        {
            base.OnSceneWasLoaded(buildIndex, sceneName);
            Init();
        }

        private void Init()
        {
            if (initStatus != Status.NotStarted)
                return;

            menuView = FindMainMenuview();
            if (menuView is null)
                return;

            menuView.Listener.ReadyForBindings += RegisterEvents;

            initStatus = Status.WaitingForEventRegistration;
        }
        private CohtmlView FindMainMenuview()
        {
            var objects = UnityEngine.Object.FindObjectsOfType<CohtmlView>();
            foreach (var view in objects)
            {
                // Apparantly only CohtmlWorldView is the view connected to the in-game menu, and not CohtmlHud (which is probably the HUD)
                if (String.Equals(view.gameObject.name, "CohtmlWorldView", StringComparison.OrdinalIgnoreCase))
                {
                    return view;
                }
            }

            return null;
        }

        private void RegisterEvents()
        {
            var view = FindMainMenuview();
            if (view is null || !view.View.IsReadyForBindings())
                return;

            view.View.RegisterForEvent("LoadInstanceDetails", new Action(() =>
            {
                MelonLogger.Msg("Instance detail loaded event!");
            }));

            initStatus = Status.Finished;
        }
    }
}
