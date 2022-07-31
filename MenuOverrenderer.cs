using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Resources;
using BepInEx;
using BepInEx.Configuration;
using cohtml;
using UnityEngine;
using UnityEngine.Rendering;
using UnityEngine.SceneManagement;

namespace MenuOverrenderer
{
    [BepInPlugin("dev.syvertsen.plugins.menuoverrenderer", "MenuOverrenderer", "0.0.1")]
    [BepInProcess("ChilloutVR.exe")]
    public class MenuOverrenderer : BaseUnityPlugin
    {
        private const string MainMenuViewName = "CohtmlWorldView";
        private const string PlayerLocalTransformName = "_PLAYERLOCAL";
        private static readonly int RenderDisabledLayer = LayerMask.NameToLayer("Ignore Raycast");
        private GameObject _mainMenu;
        private Camera _overrenderCamera;
        private RenderReplacer _replacerInstance = null;
        private int _prevCullingMask;
        private int _prevMenuLayer;

        private void Awake()
        {
            Logger.LogInfo("Hello world!");
            if (Init())
                SceneManager.sceneLoaded += OnSceneWasLoaded;
        }

        private bool Init()
        {
            _mainMenu = FindMainMenu();
            if (!_mainMenu)
                return false;

            _replacerInstance = _mainMenu.AddComponent<RenderReplacer>();
            if (!_replacerInstance)
                return false;
            _replacerInstance.LoggerCallback += (msg) => Logger.LogDebug(msg);

            if (Camera.main)
            {
                // Camera.main.cullingMask = -0b00000000000000000001000000000011;
                // _prevCullingMask = Camera.main.cullingMask;
                // // Disable the "Ignore Raycast" built-in layer.
                // Camera.main.cullingMask = ~(1 << Rend    erDisabledLayer) & Camera.main.cullingMask;
                // // _overrenderCamera = Camera.main.gameObject.AddComponent<Camera>();
                //
                // // While rendering, move the menus to the disabled layer
                // Camera.onPreCull += CameraPreRender;
                // Camera.onPostRender += CameraPreRender;
            }

            if (!_overrenderCamera)
                return false;

            Logger.LogInfo("MenuOverrenderer done initializing!");
            return true;
        }

        public void OnSceneWasLoaded(Scene scene, LoadSceneMode mode)
        {
            if (Init())
                SceneManager.sceneLoaded -= OnSceneWasLoaded;
        }

        private void OnDestroy()
        {
            Logger.LogInfo("Goodbye world!");
            if (_replacerInstance)
                Destroy(_replacerInstance);

            // if (Camera.main)
            //     Camera.main.cullingMask = _prevCullingMask;
        }

        private static GameObject FindMainMenu()
        {
            var objects = FindObjectsOfType<CohtmlView>();
            return objects.FirstOrDefault(view =>
                    string.Equals(view.gameObject.name, MainMenuViewName, StringComparison.OrdinalIgnoreCase))
                ?.gameObject;
        }

        // private void CameraPreRender(Camera _)
        // {
        //     _prevMenuLayer = _mainMenu.layer;
        //     _mainMenu.layer = LayerMask.NameToLayer("Ignore Raycast");
        // }
        //
        // private void CameraPostRender(Camera _)
        // {
        //     _mainMenu.layer = _prevMenuLayer;
        // }
    }

    class RenderReplacer : MonoBehaviour
    {
        public Action<String> LoggerCallback;
        private Renderer[] _mRenderers;
        private Dictionary<Camera, CommandBuffer> _cameras = new Dictionary<Camera, CommandBuffer>();

        private void Awake()
        {
            _mRenderers = GetComponentsInChildren<Renderer>();
        }

        // Big inspiration taken from https://forum.unity.com/threads/rendering-an-object-multiple-times-with-different-materials.505138/
        private void OnWillRenderObject()
        {
            var cam = Camera.current;
            if (_cameras.ContainsKey(cam))
                return;

            LoggerCallback?.Invoke($"Current buffer count: {cam.commandBufferCount}");

            // Build command buffer
            var cb = new CommandBuffer();
            var shaderID = Shader.PropertyToID("_MenuOverlay");
            cb.GetTemporaryRT(shaderID, -1, -1, 16, FilterMode.Bilinear);
            cb.SetRenderTarget(shaderID);
            cb.ClearRenderTarget(true, true, Color.cyan, 0.0f);

            foreach (var rend in _mRenderers)
                cb.DrawRenderer(rend, rend.material);

            // cb.SetRenderTarget(BuiltinRenderTextureType.CameraTarget);
            cb.Blit(shaderID, BuiltinRenderTextureType.CameraTarget);
            cb.ReleaseTemporaryRT(shaderID);

            // Add command buffer
            cam.AddCommandBuffer(CameraEvent.AfterEverything, cb);
            _cameras[cam] = cb;

            LoggerCallback?.Invoke("Added command buffer!");
            LoggerCallback?.Invoke($"Current buffer count: {cam.commandBufferCount}");
        }

        private void OnDestroy()
        {
            foreach (var (cam, cb) in _cameras)
                cam.RemoveCommandBuffer(CameraEvent.AfterEverything, cb);
        }
    }
}