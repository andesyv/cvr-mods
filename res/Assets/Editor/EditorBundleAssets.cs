/**
 * Editor script to bundle assets into an assetbundle
 * See https://docs.unity3d.com/Manual/AssetBundles-Workflow.html
 */
using UnityEngine;
using UnityEditor;
using System;

public class BuildAssetBundlesEditorScript : MonoBehaviour
{
    [MenuItem("MenuOverrenderer/Build Asset Bundles")]
    static void BuildAssetBundles()
    {
        // Put the bundles in a folder called "Bundle" within the Assets folder.
        var manifest = BuildPipeline.BuildAssetBundles("Assets/Bundle", BuildAssetBundleOptions.None, BuildTarget.StandaloneWindows);
        string contents = "";
        if (manifest) {
            var manifestList = manifest.GetAllAssetBundles();
            foreach (var name in manifestList)
                contents += String.Format("{0}, ", name);

            Debug.Log(String.Format("Manifest includes: {0}", contents));
        } else
        {
            throw new Exception("Failed to build asset bundles");
        }
    }
}