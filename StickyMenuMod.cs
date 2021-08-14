using System;
using ABI_RC.Core.InteractionSystem;
using cohtml;
using cohtml.Net;
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
        private int ConstraintSourceIndex;
        private bool Enabled = false;
        private Config config;

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

            config = new Config();

            SetupConstraint();

            MethodPatcher.OnMenuEnabled += EnableConstraint;
            MethodPatcher.OnMenuDisabled += DisableConstraint;
            MethodPatcher.DoPatching();

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

        private void SetupConstraint()
        {
            Constraint = MenuView.gameObject.AddComponent<ParentConstraint>();
            Constraint.constraintActive = false;
            Constraint.weight = 1f;

            ConstraintSource source = new ConstraintSource();
            source.sourceTransform = PlayerLocalTransform;
            source.weight = 1f;
            ConstraintSourceIndex = Constraint.AddSource(source);

            config.enabled.OnValueChanged += (bool oldVal, bool newVal) =>
            {
                if (Enabled && newVal)
                    Constraint.constraintActive = Enabled = false;
            };
        }

        private void EnableConstraint()
        {
            if (Enabled || !config.enabled.Value)
                return;

            Enabled = true;

            Vector3 diffDist = MenuView.transform.position - PlayerLocalTransform.position;
            // Convert to local difference (local difference is always a constant, so could replace this computation...)
            diffDist = PlayerLocalTransform.worldToLocalMatrix * diffDist;
            Constraint.SetTranslationOffset(ConstraintSourceIndex, diffDist);

            Vector3 diffRot = MenuView.transform.rotation.eulerAngles - PlayerLocalTransform.rotation.eulerAngles;
            Constraint.SetRotationOffset(ConstraintSourceIndex, diffRot);
            Constraint.constraintActive = true;
        }

        private void DisableConstraint()
        {
            if (!Enabled || !config.enabled.Value)
                return;

            Enabled = false;
            Constraint.constraintActive = false;
        }
    }
}
