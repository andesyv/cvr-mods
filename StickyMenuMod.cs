using System;
using System.Reflection;
using ABI.CCK.Components;
using ABI_RC.Core.InteractionSystem;
using ABI_RC.Core.Savior;
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
        private bool Enabled = false;
        public static bool Dragging = false;
        private Config config;
        private CVRPickupObject Pickupable;
        private MethodInfo GrabObjectMethod;

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

            GrabObjectMethod = typeof(ControllerRay).GetMethod("GrabObject", BindingFlags.Instance | BindingFlags.NonPublic, null, new Type[] { typeof(CVRPickupObject), typeof(RaycastHit) }, null);

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

            MenuView.View.RegisterForEvent("all", new Action<string, Value[]>((string str, Value[] vals) => MelonLogger.Msg("Button clicked! Str: {0}", str)));

            // Attach a "button clicked" event to all event handlers in 
            MenuView.View.RegisterForEvent("CVRNoButtonClicked", new Action(() =>
            {
                MelonLogger.Msg("No buttons clicked!");
                GrabStart();
            }));
            MethodPatcher.OnMenuMouseUp += GrabEnd;


            MelonLogger.Msg("Init done!");

            InitStatus = Status.Finished;
        }

        private void SetupConstraint()
        {
            // Simply parent for now (could create some better logic in terms of rotation and translation in future)
            MenuView.transform.parent = PlayerLocalTransform;

            var collider = MenuView.gameObject.GetComponent<MeshCollider>();
            collider.enabled = true;
            collider.convex = false;
            // TODO: Find a way to either use convex mesh collider, force rigidbody as kinematic, or use another collider in menu raytrace
            /*var collider = MenuView.gameObject.AddComponent<BoxCollider>();
            collider.size = MenuView.gameObject.GetComponent<MeshRenderer>().bounds.size;*/
            MenuView.gameObject.AddComponent<CVRInteractable>();
            Pickupable = MenuView.gameObject.AddComponent<CVRPickupObject>();
            MenuView.gameObject.GetComponent<Rigidbody>().isKinematic = true;
        }

        private void EnableConstraint()
        {
            if (Enabled || !config.enabled.Value)
                return;

            Enabled = true;
            MelonLogger.Msg("Enabled!");
            

            /*Vector3 diffDist = MenuView.transform.position - PlayerLocalTransform.position;
            // Convert to local difference (local difference is always a constant, so could replace this computation...)
            diffDist = PlayerLocalTransform.worldToLocalMatrix * diffDist;
            Constraint.SetTranslationOffset(ConstraintSourceIndex, diffDist);

            Vector3 diffRot = MenuView.transform.rotation.eulerAngles - PlayerLocalTransform.rotation.eulerAngles;
            Constraint.SetRotationOffset(ConstraintSourceIndex, diffRot);
            Constraint.constraintActive = true;*/
            MelonLogger.Msg("CVR_InteractableManager.enableInteractions: {0}", CVR_InteractableManager.enableInteractions);
            /*MetaPort.Instance*/
        }

        private void DisableConstraint()
        {
            if (!Enabled || !config.enabled.Value)
                return;

            Enabled = false;
            MelonLogger.Msg("Disabled!");
            /*Constraint.constraintActive = false;*/
        }

        private void GrabStart()
        {
            MelonLogger.Msg("Grab start!");
            MelonLogger.Msg("MethodPatcher.MouseDownOnMenu == {0}", MethodPatcher.MouseDownOnMenu);
            if (Dragging || !MethodPatcher.MouseDownOnMenu || MethodPatcher.rayInstance is null)
                return;

            Dragging = true;
            
            if (Pickupable is null)
            {
                MelonLogger.Error("Pickupable is null!");
                return;
            }

            GrabObjectMethod.Invoke(MethodPatcher.rayInstance, new object[] {Pickupable, MethodPatcher.hitInfo});
        }

        private void GrabEnd()
        {
            MelonLogger.Msg("Grab end!");
            MelonLogger.Msg("MethodPatcher.MouseDownOnMenu == {0}", MethodPatcher.MouseDownOnMenu);
            if (!Dragging || MethodPatcher.rayInstance is null)
                return;

            Dragging = false;
            MethodPatcher.rayInstance.DropObject();
        }
    }
}
