using ABI_RC.Core.IO;
using MelonLoader;
using System;
using UnityEngine;

namespace VideoCalls
{
  public class VideoProjector : MonoBehaviour
  {
    public VideoCalls.ImageData CurrentImage
    {
      get { return mCurrentImage; }
      set
      {
        mCurrentImage = value;
        mTextureGenerated = false;
      }
    }

    private VideoCalls.ImageData mCurrentImage;
    private bool mTextureGenerated = true;
    private Texture2D mTexture;
    private Material mRenderMaterial;

    private static TextureFormat ConvertTextureFormat(RenderTextureFormat format)
    {
      if (format == RenderTextureFormat.ARGB32)
        return TextureFormat.ARGB32;
      if (format == RenderTextureFormat.ARGB4444)
        return TextureFormat.ARGB4444;
        
      MelonLogger.Error("Format has not been accounted for!");
      throw new InvalidOperationException("Format has not been accounted for!");
    }

    private void Start()
    {
      mRenderMaterial = new Material(Shader.Find("Unlit/Transparent Cutout"));
      GetComponent<Renderer>().material = mRenderMaterial;
      mRenderMaterial.SetTexture("_MainTex", Texture2D.blackTexture);
    }

    private void Update()
    {
      // Nothing to do
      if (mTextureGenerated)
        return;

      MelonLogger.Msg($"Created a new {mCurrentImage.mWidth} x {mCurrentImage.mHeight} texture");
      mTexture = new Texture2D(mCurrentImage.mWidth, mCurrentImage.mHeight, ConvertTextureFormat(mCurrentImage.mFormat), -1, false);
      mTexture.LoadRawTextureData(mCurrentImage.mData);
      mTexture.Apply();
      mTextureGenerated = true;

      mRenderMaterial.SetTexture("_MainTex", mTexture);
    }
  }
}