using System;
using cohtml;
using MelonLoader;
using UnityEngine;
using UnityEngine.Animations;

namespace StickyMenu
{
    public class StickyMenuMod : MelonMod
    {
        private const string MainMenuViewName = "CohtmlWorldView";
        private const string PlayerLocalTransformName = "_PLAYERLOCAL";

        enum Status
        {
            NotStarted,
            WaitingForEventRegistration,
            Finished
        }

        private Status InitStatus = Status.NotStarted;
        private CohtmlView MenuView = null;
        private Transform PlayerLocalTransform = null;
        private ParentConstraint Constraint;

        public override void OnSceneWasLoaded(int buildIndex, string sceneName)
        {
            base.OnSceneWasLoaded(buildIndex, sceneName);
            Init();
        }

        private void Init()
        {
            if (InitStatus != Status.NotStarted)
                return;

            if (MenuView is null)
                MenuView = FindMainMenuview();

            if (PlayerLocalTransform is null)
                PlayerLocalTransform = FindPlayerTransform();

            if (MenuView is null || PlayerLocalTransform is null)
                return;

            Constraint = MenuView.gameObject.AddComponent<ParentConstraint>();
            Constraint.constraintActive = false;


            MenuView.Listener.ReadyForBindings += RegisterEvents;

            InitStatus = Status.WaitingForEventRegistration;
        }

        private CohtmlView FindMainMenuview()
        {
            var objects = UnityEngine.Object.FindObjectsOfType<CohtmlView>();
            foreach (var view in objects)
            {
                // Apparantly only CohtmlWorldView is the view connected to the in-game menu, and not CohtmlHud (which is probably the HUD)
                if (String.Equals(view.gameObject.name, MainMenuViewName, StringComparison.OrdinalIgnoreCase))
                {
                    return view;
                }
            }

            return null;
        }

        private Transform FindPlayerTransform()
        {
            var obj = PersistentObjectFinder.Find(PlayerLocalTransformName, StringComparison.OrdinalIgnoreCase);
            return obj?.transform;
        }

        private void RegisterEvents()
        {
            if (MenuView is null || !MenuView.View.IsReadyForBindings())
                return;

            MenuView.View.RegisterForEvent("LoadInstanceDetails", new Action(() =>
            {
                MelonLogger.Msg("Instance detail loaded event!");
            }));

            MelonLogger.Msg("Init done!");

            InitStatus = Status.Finished;
        }
    }
}
