using System;
using System.Linq;
using ABI.CCK.Components;
using BepInEx;
using cohtml;
using cohtml.Net;
using UnityEngine;
using UnityEngine.SceneManagement;

namespace StickyMenu
{
    [BepInPlugin(MyPluginInfo.PLUGIN_GUID, MyPluginInfo.PLUGIN_NAME, MyPluginInfo.PLUGIN_VERSION)]
    [BepInProcess("ChilloutVR.exe")]
    public class StickyMenuMod : BaseUnityPlugin
    {
        private const string MainMenuViewName = "CohtmlWorldView";
        private const string PlayerLocalTransformName = "_PLAYERLOCAL";

        public static bool Dragging;
        public BoxCollider DragCollider;
        private bool _enabled;

        private Status _initStatus = Status.NotStarted;
        private CohtmlView _menuView;
        private BoundEventHandle? _noButtonEventHandle;
        private Offset _offset;
        private CVRPickupObject _pickupable;
        private Transform _playerLocalTransform;
        public Config StickyMenuConfig { get; private set; }

        public static StickyMenuMod Instance { get; private set; }

        private void Awake()
        {
            Logger.LogInfo("Init started!");
            Instance = this;

            Init();
            // Attempt to init every time we load a scene until we have successfully initialized
            if (_initStatus != Status.Finished)
                SceneManager.sceneLoaded += OnSceneWasLoaded;
        }

        private void FixedUpdate()
        {
            if (!_enabled || Dragging || _initStatus != Status.Finished || !StickyMenuConfig.Enabled.Value)
                return;

            var posOffset = _playerLocalTransform.TransformVector(_offset.LocalPosition);
            if (StickyMenuConfig.LockPosition.Value)
                _menuView.transform.position = _playerLocalTransform.position + posOffset;

            if (StickyMenuConfig.LockRotation.Value)
                _menuView.transform.rotation = _offset.Rotation * _playerLocalTransform.rotation;
        }

        private void OnDestroy()
        {
            GrabEnd();
            ObliterateConstraints();
            UnregisterEvents();
            MethodPatcher.UndoPatching();
            SceneManager.sceneLoaded -= OnSceneWasLoaded;

            _initStatus = Status.NotStarted;
        }

        public void OnSceneWasLoaded(Scene scene, LoadSceneMode mode)
        {
            Init();
            if (_initStatus == Status.Finished)
                SceneManager.sceneLoaded -= OnSceneWasLoaded;
        }

        private void Init()
        {
            if (_initStatus != Status.NotStarted)
                return;

            if (_menuView is null)
                _menuView = FindMainMenuView();

            if (_playerLocalTransform is null)
                _playerLocalTransform = FindPlayerTransform();

            if (_menuView is null || _playerLocalTransform is null)
                return;

            StickyMenuConfig = new Config(Config);

            SetupConstraint();

            MethodPatcher.OnMenuEnabled += EnableConstraint;
            MethodPatcher.OnMenuDisabled += DisableConstraint;
            MethodPatcher.DoPatching();

            _initStatus = Status.WaitingForEventRegistration;
            if (_menuView.View.IsReadyForBindings())
                RegisterEvents();
            else
                _menuView.Listener.ReadyForBindings += RegisterEvents;
        }

        private static CohtmlView FindMainMenuView()
        {
            var objects = FindObjectsOfType<CohtmlView>();
            return objects.FirstOrDefault(view =>
                string.Equals(view.gameObject.name, MainMenuViewName, StringComparison.OrdinalIgnoreCase));
        }

        private static Transform FindPlayerTransform()
        {
            var obj = PersistentObjectFinder.Find(PlayerLocalTransformName, StringComparison.OrdinalIgnoreCase);
            return obj?.transform;
        }

        private void RegisterEvents()
        {
            if (_menuView is null || !_menuView.View.IsReadyForBindings())
                return;

            if (StickyMenuConfig.UseEdgeDragging.Value)
                MethodPatcher.OnGrabMenu += GrabStart;
            else
                _noButtonEventHandle = _menuView.View.RegisterForEvent("CVRNoButtonClicked", new Action(GrabStart));

            MethodPatcher.OnMenuMouseUp += GrabEnd;


            Logger.LogInfo("Init done!");

            _initStatus = Status.Finished;
        }

        private void UnregisterEvents()
        {
            if (!StickyMenuConfig.UseEdgeDragging.Value && _noButtonEventHandle != null)
                _menuView.View.UnregisterFromEvent((BoundEventHandle) _noButtonEventHandle);
        }

        private void SetupConstraint()
        {
            var collider = _menuView.gameObject.GetComponent<MeshCollider>();
            var rigidbody = _menuView.gameObject.GetComponent<Rigidbody>();
#if HOT_RELOADING
            _colliderEnabledOriginal = collider.enabled;
            _colliderConvexOriginal = collider.convex;
            _rigidbodyIsKinematicOriginal = rigidbody.isKinematic;
#endif
            collider.enabled = true;
            collider.convex = false;
            DragCollider = _menuView.gameObject.AddComponent<BoxCollider>();
            DragCollider.isTrigger = true;
            var innerBounds = _menuView.gameObject.GetComponent<MeshRenderer>().bounds.size;
            DragCollider.size = new Vector3(1.1F, 1.1F, 0.2F);
            _menuView.gameObject.AddComponent<CVRInteractable>();
            _pickupable = _menuView.gameObject.AddComponent<CVRPickupObject>();
            rigidbody.isKinematic = true;
            DragCollider.enabled = false;
        }

        private void ObliterateConstraints()
        {
            Destroy(_pickupable);
            Destroy(_menuView.gameObject.GetComponent<CVRInteractable>());
            Destroy(DragCollider);

            // Undo all settings we changed:
#if HOT_RELOADING
            var collider = _menuView.gameObject.GetComponent<MeshCollider>();
            var rigidbody = _menuView.gameObject.GetComponent<Rigidbody>();

            collider.enabled = _colliderEnabledOriginal;
            collider.convex = _colliderConvexOriginal;
            rigidbody.isKinematic = _rigidbodyIsKinematicOriginal;
#endif
        }

        private void EnableConstraint()
        {
            if (_enabled || !StickyMenuConfig.Enabled.Value)
                return;

            UpdateOffset();

            _enabled = true;
            DragCollider.enabled = true;
        }

        private void DisableConstraint()
        {
            if (!_enabled)
                return;

            _enabled = false;
            DragCollider.enabled = false;
        }

        private void GrabStart()
        {
            if (Dragging || !StickyMenuConfig.EnableDragging.Value || !MethodPatcher.MouseDownOnMenu ||
                MethodPatcher.RayInstance is null)
                return;

            Dragging = true;

            if (_pickupable is null)
            {
                Logger.LogError("_pickupable is null!");
                return;
            }

            if (!StickyMenuConfig.UseEdgeDragging.Value)
                MethodPatcher.GrabObjectMethod?.Invoke(MethodPatcher.RayInstance,
                    new object[] {_pickupable, MethodPatcher.HitInfo});
        }

        private void GrabEnd()
        {
            if (!Dragging || MethodPatcher.RayInstance is null)
                return;

            Dragging = false;
            MethodPatcher.RayInstance.DropObject();

            UpdateOffset();
        }

        private void UpdateOffset()
        {
            _offset.Position = _menuView.transform.position - _playerLocalTransform.position;
            _offset.LocalPosition = _playerLocalTransform.InverseTransformVector(_offset.Position);
            _offset.Rotation = _menuView.transform.rotation * Quaternion.Inverse(_playerLocalTransform.rotation);
        }

        private enum Status
        {
            NotStarted,
            WaitingForEventRegistration,
            Finished
        }

        private struct Offset
        {
            public Vector3 Position;
            public Vector3 LocalPosition;
            public Quaternion Rotation;
        }

#if HOT_RELOADING
        private bool _colliderEnabledOriginal;
        private bool _colliderConvexOriginal;
        private bool _rigidbodyIsKinematicOriginal;
#endif
    }
}