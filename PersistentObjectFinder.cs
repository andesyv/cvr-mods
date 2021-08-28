using System;
using System.Linq;
using UnityEngine;
using Object = UnityEngine.Object;

namespace StickyMenu
{
    internal class PersistentObjectFinder
    {
        private static PersistentObjectFinder _instance = null;
        private GameObject _spy;
        private static PersistentObjectFinder Instance => _instance ?? (_instance = new PersistentObjectFinder());

        private PersistentObjectFinder()
        {
            _spy = new GameObject("PersistentObjectFinder");
            Object.DontDestroyOnLoad(_spy);
        }

        public static GameObject Find(string name, StringComparison stringComparisonType = StringComparison.Ordinal)
        {
            var result = Instance.FindLocal(name, stringComparisonType);
            // Most likely just going to use finder until we find something, so clean up afterwards
            if (!(result is null))
                _instance = null;
            return result;
        }

        private GameObject FindLocal(string name, StringComparison stringComparisonType = StringComparison.Ordinal)
        {
            var roots = _spy.scene.GetRootGameObjects();
            return roots.FirstOrDefault(obj => obj != _spy && string.Equals(obj.name, name, stringComparisonType));
        }

        ~PersistentObjectFinder()
        {
            Object.Destroy(_spy);
            _spy = null;
        }
    }
}
