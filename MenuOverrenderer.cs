using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Resources;
using BepInEx;
using BepInEx.Configuration;
using BepInEx.Logging;
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
        private RenderReplacer _replacerInstance = null;
        private AssetBundle _assets;
        public static Material UIMaterial;

        private void Awake()
        {
            if (Init())
                SceneManager.sceneLoaded += OnSceneWasLoaded;
        }

        private bool Init()
        {
            _mainMenu = FindMainMenu();
            if (!_mainMenu)
                return false;

            if (!LoadAssets())
                return false;

            _replacerInstance = _mainMenu.AddComponent<RenderReplacer>();
            if (!_replacerInstance)
                return false;

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

            // if (!_overrenderCamera)
            //     return false;

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
            
            if (UIMaterial)
                Destroy(UIMaterial);
            if (_assets)
                _assets.Unload(true);
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

        private bool LoadAssets()
        {
            Logger.LogDebug("Loading assets");
            // Very cool method of embedding assets into dll. Code taken from
            // https://github.com/knah/VRCMods/blob/master/UIExpansionKit/UiExpansionKitMod.cs#L105
            // and
            // https://github.com/sinai-dev/UnityExplorer/blob/master/src/UI/UIManager.cs#L446
            var stream = Assembly.GetExecutingAssembly()
                .GetManifestResourceStream("MenuOverrenderer.Resources.Assets.Bundle.menuoverrenderer.bundle");
            if (stream == null)
            {
                Logger.LogError("Failed to fetch reasource stream from assembly!");
                return false;
            }

            // var memStream = new MemoryStream((int)stream.Length);
            // stream.CopyTo(memStream);
            // _assets = AssetBundle.LoadFromMemory(memStream.ToArray(), 0);
            _assets = AssetBundle.LoadFromStream(stream);

            if (!_assets)
            {
                Logger.LogError("Failed to load assetbundle!");
                return false;
            }

            _assets.hideFlags |= HideFlags.DontUnloadUnusedAsset;

            UIMaterial = _assets.LoadAsset<Material>("ui.mat");
            if (!UIMaterial)
            {
                Logger.LogError("Failed to fetch material from assetbundle!");
                return false;
            }

            UIMaterial.hideFlags |= HideFlags.DontUnloadUnusedAsset;
            UIMaterial.EnableKeyword("UNITY_UV_STARTS_AT_TOP");
            
            Logger.LogDebug("Finished loading assets");

            return true;
        }
    }

    class RenderReplacer : MonoBehaviour
    {
        private Renderer[] _mRenderers;
        private Dictionary<Camera, CommandBuffer> _cameras = new Dictionary<Camera, CommandBuffer>();
        private Material _material;
        private CohtmlView _view;
        private ManualLogSource _logger = BepInEx.Logging.Logger.CreateLogSource("RenderReplacer");

        RenderReplacer()
        {
            _material = MenuOverrenderer.UIMaterial;
        }

        private void Awake()
        {
            _mRenderers = GetComponentsInChildren<Renderer>();
            _view = GetComponent<CohtmlView>();
            if (!_view)
                _logger.LogError("View was not found!");
        }

        // Big inspiration taken from https://forum.unity.com/threads/rendering-an-object-multiple-times-with-different-materials.505138/
        private void OnWillRenderObject()
        {
            var cam = Camera.current;
            if (_cameras.ContainsKey(cam))
                return;

            _logger.LogDebug($"Current buffer count: {cam.commandBufferCount}");

            // Build command buffer
            var cb = new CommandBuffer();
            var shaderID = Shader.PropertyToID("_MenuOverlay");
            cb.GetTemporaryRT(shaderID, -1, -1, 16, FilterMode.Bilinear);
            cb.SetRenderTarget(shaderID);
            cb.ClearRenderTarget(true, true, Color.cyan, 0.0f);

            foreach (var rend in _mRenderers)
            {
                if (_view)
                {
                    cb.SetGlobalTexture("_GlobalUITexture", _view.ViewTexture);
                    _logger.LogDebug($"_GlobalUITexture was set to {_view.ViewTexture.name}");
                }
                cb.DrawRenderer(rend, _material);
            }

            // cb.SetRenderTarget(BuiltinRenderTextureType.CameraTarget);
            cb.Blit(shaderID, BuiltinRenderTextureType.CameraTarget);
            cb.ReleaseTemporaryRT(shaderID);

            // Add command buffer
            cam.AddCommandBuffer(CameraEvent.AfterEverything, cb);
            _cameras[cam] = cb;

            _logger.LogDebug("Added command buffer!");
            _logger.LogDebug($"Current buffer count: {cam.commandBufferCount}");
        }

        private void OnDestroy()
        {
            foreach (var (cam, cb) in _cameras)
                cam.RemoveCommandBuffer(CameraEvent.AfterEverything, cb);
        }
    }
}