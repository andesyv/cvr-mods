using System.Collections.Generic;
using System.Linq;
using BepInEx.Logging;
using cohtml;
using UnityEngine;
using UnityEngine.Rendering;

namespace MenuReRender
{
    internal class RenderReplacer : MonoBehaviour
    {
        private Renderer[] _renderers;
        private bool[] _enabledStates;
        private readonly Dictionary<Camera, CommandBuffer> _cameras = new();
        public Material material;
        private RenderTexture _menuRenderTexture;
        private ManualLogSource Logger { get; } = BepInEx.Logging.Logger.CreateLogSource("RenderReplacer");
        public Animator animator;

        private void Awake()
        {
            _renderers = GetComponentsInChildren<Renderer>();
            _enabledStates = _renderers.Select(r => r.enabled).ToArray();
            var view = GetComponent<CohtmlView>();
            if (!view)
                Logger.LogError("View was not found!");
            else
                _menuRenderTexture = view.ViewTexture;
        }

        private bool IsVisible()
        {
            if (!animator)
                return true;
            var clips = animator.GetCurrentAnimatorClipInfo(0);
            return 0 < clips.Count(c => c.clip.name is "Open");
        }

        // Inspiration for using command buffers taken from
        // https://forum.unity.com/threads/rendering-an-object-multiple-times-with-different-materials.505138/
        private void OnWillRenderObject()
        {
            var cam = Camera.current;
            if (_cameras.ContainsKey(cam))
            {
                // Something keeps re-enabling the renderers each frame (probably with good reason),
                // but we have to disable em so we don't render twice
                foreach (var rend in _renderers)
                    rend.enabled = false;

                return;
            }

            if (!IsVisible())
                return;
            
            // Build command buffer
            var cb = new CommandBuffer();
            // var shaderID = Shader.PropertyToID("_MenuOverlay");
            // cb.GetTemporaryRT(shaderID, -1, -1, 16, FilterMode.Bilinear);
            // cb.SetRenderTarget(shaderID);
            // cb.ClearRenderTarget(true, true, Color.cyan, 0.0f);
            cb.SetRenderTarget(BuiltinRenderTextureType.CameraTarget);

            foreach (var rend in _renderers)
            {
                if (_menuRenderTexture)
                    cb.SetGlobalTexture("_CurrentUITexture", _menuRenderTexture);

                cb.DrawRenderer(rend, material);
                rend.enabled = false;
            }

            // cb.SetRenderTarget(BuiltinRenderTextureType.CameraTarget);
            // cb.Blit(shaderID, BuiltinRenderTextureType.CameraTarget);
            // cb.ReleaseTemporaryRT(shaderID);

            // Add command buffer
            cam.AddCommandBuffer(CameraEvent.AfterEverything, cb);
            _cameras[cam] = cb;
            
            Logger.LogDebug("Added command buffer!");
        }

        private void OnDisable()
        {
            foreach (var (cam, cb) in _cameras)
                cam.RemoveCommandBuffer(CameraEvent.AfterEverything, cb);
            _cameras.Clear();

            _renderers.Zip(_enabledStates, (rend, enable) => rend.enabled = enable);
            Logger.LogDebug("Removed command buffer!");
        }
    }
}