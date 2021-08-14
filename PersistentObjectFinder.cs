using System;
using UnityEngine;

namespace StickyMenu
{
    class PersistentObjectFinder
    {
        private static PersistentObjectFinder instance = null;
        private GameObject spy;
        private static PersistentObjectFinder Instance
        {
            get
            {
                if (instance is null)
                    instance = new PersistentObjectFinder();
                return instance;
            }
        }

        private PersistentObjectFinder()
        {
            spy = new GameObject("PersistentObjectFinder");
            GameObject.DontDestroyOnLoad(spy);
        }

        public static GameObject Find(string name, StringComparison stringComparisonType = StringComparison.Ordinal)
        {
            var result = Instance.FindLocal(name, stringComparisonType);
            // Most likely just going to use finder until we find something, so clean up afterwards
            if (!(result is null))
                instance = null;
            return result;
        }

        private GameObject FindLocal(string name, StringComparison stringComparisonType = StringComparison.Ordinal)
        {
            var roots = spy.scene.GetRootGameObjects();
            foreach (var obj in roots)
                if (obj != spy && String.Equals(obj.name, name, stringComparisonType))
                    return obj;
            return null;
        }

        ~PersistentObjectFinder()
        {
            GameObject.Destroy(spy);
            spy = null;
        }
    }
}
