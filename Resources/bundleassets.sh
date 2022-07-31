#!/usr/bin/env bash
BASEDIR=$(dirname "$0")

unity -quit -batchmode -executeMethod BuildAssetBundlesEditorScript.BuildAssetBundles -projectPath $BASEDIR