using System;
using MelonLoader;

namespace StickyMenu
{
    class Config
    {
        private MelonPreferences_Category generalCategory;
        public MelonPreferences_Entry<bool> enabled;
        public MelonPreferences_Entry<bool> lockPosition;
        public MelonPreferences_Entry<bool> lockRotation;
        public MelonPreferences_Entry<bool> enableDragging;

        public Config()
        {
            generalCategory = MelonPreferences.CreateCategory("StickyMenu", "StickyMenu");
            generalCategory.LoadFromFile();

            enabled = generalCategory.CreateEntry("Enabled", true);
            lockPosition = generalCategory.CreateEntry("LockMenuPosition", true);
            lockRotation = generalCategory.CreateEntry("LockMenuRotation", true);
            enableDragging = generalCategory.CreateEntry("EnableMenuDragging", true);

            generalCategory.SaveToFile(false);
        }

        ~Config()
        {
            generalCategory.SaveToFile();
        }
    }
}
