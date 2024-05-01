using System.Collections.Generic;
using System.Linq;
using MelonLoader;
using cohtml;
using UnityEngine;
using UnityEngine.Rendering;

namespace MenuReRender
{
  internal class RenderReplacer : MonoBehaviour
  {
    public Material material;
    public Animator animator;

    private Renderer[] _renderers;
    private bool[] _enabledStates;
    private readonly Dictionary<Camera, CommandBuffer> _cameras = new();
    private RenderTexture _menuRenderTexture;

    private void Awake()
    {
      _renderers = GetComponentsInChildren<Renderer>();
      _enabledStates = _renderers.Select(r => r.enabled).ToArray();
      var view = GetComponent<CohtmlView>();
      if (!view)
        MelonLogger.Error("View was not found!");
      else
        _menuRenderTexture = view.ViewTexture;
    }

    private bool IsVisible()
    {
      if (!animator)
        return true;
      var clips = animator.GetCurrentAnimatorClipInfo(0);
      return clips.Any(c => c.clip.name is "Open");
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
      cb.SetRenderTarget(new RenderTargetIdentifier(BuiltinRenderTextureType.CameraTarget, 0, CubemapFace.Unknown, RenderTargetIdentifier.AllDepthSlices));

      foreach (var rend in _renderers)
      {
        if (_menuRenderTexture)
          cb.SetGlobalTexture("_CurrentUITexture", _menuRenderTexture);

        cb.DrawRenderer(rend, material);
        rend.enabled = false;
      }

      // Add command buffer
      // Note to self: CameraEvent.AfterImageEffects is the last CameraEvent before blitting to eye buffers,
      // which means it's the last event we can rely on for rendering to both eyes in single-pass instanced rendering
      cam.AddCommandBuffer(CameraEvent.AfterImageEffects, cb);
      _cameras[cam] = cb;
    }

    private void OnDisable()
    {
      foreach (var (cam, cb) in _cameras)
        cam.RemoveCommandBuffer(CameraEvent.AfterImageEffects, cb);
      _cameras.Clear();

      _renderers.Zip(_enabledStates, (rend, enable) => rend.enabled = enable);
    }
  }
}