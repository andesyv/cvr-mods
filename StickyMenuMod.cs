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

        internal struct Offset
        {
            public Vector3 position;
            public Vector3 localPosition;
            public Quaternion rotation;
        }

        private Status InitStatus = Status.NotStarted;
        private CohtmlView MenuView = null;
        private Transform PlayerLocalTransform = null;
        private bool Enabled = false;
        public static bool Dragging = false;
        private Config config;
        private CVRPickupObject Pickupable;
        private MethodInfo GrabObjectMethod;
        private Offset offset;

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

            UpdateOffset();
        }

        private void UpdateOffset()
        {
            offset.position = MenuView.transform.position - PlayerLocalTransform.position;
            offset.localPosition = PlayerLocalTransform.InverseTransformVector(offset.position);
            offset.rotation = PlayerLocalTransform.rotation * Quaternion.Inverse(MenuView.transform.rotation);
        }

        private void DisableConstraint()
        {
            if (!Enabled)
                return;

            Enabled = false;
            MelonLogger.Msg("Disabled!");
        }

        private void GrabStart()
        {
            MelonLogger.Msg("Grab start!");
            MelonLogger.Msg("MethodPatcher.MouseDownOnMenu == {0}", MethodPatcher.MouseDownOnMenu);
            if (Dragging || !config.enableDragging.Value || !MethodPatcher.MouseDownOnMenu || MethodPatcher.rayInstance is null)
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
            
            UpdateOffset();
        }

        public override void OnFixedUpdate()
        {
            if (!Enabled || Dragging || InitStatus != Status.Finished || !config.enabled.Value)
                return;

            var posOffset = config.lockRotation.Value
                ? PlayerLocalTransform.TransformVector(offset.localPosition)
                : offset.position;
            if (config.lockPosition.Value)
                MenuView.transform.position = PlayerLocalTransform.position + posOffset;

            if (config.lockRotation.Value)
                MenuView.transform.rotation = offset.rotation * PlayerLocalTransform.rotation;
        }
    }
}
