﻿using System;
using System.Linq;
using ABI.CCK.Components;
using cohtml;
using cohtml.Net;
using MelonLoader;
using UnityEngine;

namespace StickyMenu
{
    public class StickyMenuMod : MelonMod
    {
        private const string MainMenuViewName = "CohtmlWorldView";
        private const string PlayerLocalTransformName = "_PLAYERLOCAL";

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

        private Status _initStatus = Status.NotStarted;
        private CohtmlView _menuView = null;
        private Transform _playerLocalTransform = null;
        private bool _enabled = false;
        private Config _config;
        private CVRPickupObject _pickupable;
        private Offset _offset;
        private static StickyMenuMod _instance;

        public static bool Dragging = false;
        public Config Config => _config;
        public BoxCollider DragCollider = null;
        public static StickyMenuMod Instance => _instance;


        private StickyMenuMod()
        {
            _instance = this;
        }

        public override void OnSceneWasLoaded(int buildIndex, string sceneName)
        {
            base.OnSceneWasLoaded(buildIndex, sceneName);
            Init();
        }

        private void Init()
        {
            if (_initStatus != Status.NotStarted)
                return;

            if (_menuView is null)
                _menuView = FindMainMenuview();

            if (_playerLocalTransform is null)
                _playerLocalTransform = FindPlayerTransform();

            if (_menuView is null || _playerLocalTransform is null)
                return;

            _config = new Config();

            SetupConstraint();

            MethodPatcher.OnMenuEnabled += EnableConstraint;
            MethodPatcher.OnMenuDisabled += DisableConstraint;
            MethodPatcher.DoPatching();

            _menuView.Listener.ReadyForBindings += RegisterEvents;

            _initStatus = Status.WaitingForEventRegistration;
        }

        private static CohtmlView FindMainMenuview()
        {
            var objects = UnityEngine.Object.FindObjectsOfType<CohtmlView>();
            return objects.FirstOrDefault(view => string.Equals(view.gameObject.name, MainMenuViewName, StringComparison.OrdinalIgnoreCase));
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

            _menuView.View.RegisterForEvent("LoadInstanceDetails", new Action(() =>
            {
                MelonLogger.Msg("Instance detail loaded event!");
            }));

            _menuView.View.RegisterForEvent("all", new Action<string, Value[]>((string str, Value[] values) => MelonLogger.Msg("Button clicked! Str: {0}", str)));
            if (Config.UseEdgeDragging.Value)
            {
                MethodPatcher.OnMenuMouseDown += GrabStart;
            }
            else
            {
                _menuView.View.RegisterForEvent("CVRNoButtonClicked", new Action(() =>
                {
                    MelonLogger.Msg("No buttons clicked!");
                    GrabStart();
                }));
            }
            
            MethodPatcher.OnMenuMouseUp += GrabEnd;


            MelonLogger.Msg("Init done!");

            _initStatus = Status.Finished;
        }

        private void SetupConstraint()
        {
            var collider = _menuView.gameObject.GetComponent<MeshCollider>();
            collider.enabled = true;
            collider.convex = false;
            DragCollider = _menuView.gameObject.AddComponent<BoxCollider>();
            DragCollider.isTrigger = true;
            var innerBounds = _menuView.gameObject.GetComponent<MeshRenderer>().bounds.size;
            DragCollider.size = new Vector3(innerBounds.x * 0.7F, innerBounds.y * 1.3F, 0.2F);
            _menuView.gameObject.AddComponent<CVRInteractable>();
            _pickupable = _menuView.gameObject.AddComponent<CVRPickupObject>();
            _menuView.gameObject.GetComponent<Rigidbody>().isKinematic = true;
            DragCollider.enabled = Config.UseEdgeDragging.Value;
        }

        private void EnableConstraint()
        {
            if (_enabled || !_config.Enabled.Value)
                return;

            _enabled = true;

            UpdateOffset();
        }

        private void DisableConstraint()
        {
            if (!_enabled)
                return;

            _enabled = false;
        }

        private void GrabStart()
        {
            if (Dragging || !_config.EnableDragging.Value || !MethodPatcher.MouseDownOnMenu || MethodPatcher.RayInstance is null)
                return;

            Dragging = true;
            
            if (_pickupable is null)
            {
                MelonLogger.Error("_pickupable is null!");
                return;
            }

            MethodPatcher.GrabObjectMethod.Invoke(MethodPatcher.RayInstance, new object[] {_pickupable, MethodPatcher.HitInfo});
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
            _offset.Rotation = _playerLocalTransform.rotation * Quaternion.Inverse(_menuView.transform.rotation);
        }

        public override void OnFixedUpdate()
        {
            if (!_enabled || Dragging || _initStatus != Status.Finished || !_config.Enabled.Value)
                return;

            var posOffset = _config.LockRotation.Value
                ? _playerLocalTransform.TransformVector(_offset.LocalPosition)
                : _offset.Position;
            if (_config.LockPosition.Value)
                _menuView.transform.position = _playerLocalTransform.position + posOffset;

            if (_config.LockRotation.Value)
                _menuView.transform.rotation = _offset.Rotation * _playerLocalTransform.rotation;
        }
    }
}
