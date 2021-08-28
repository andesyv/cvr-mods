using MelonLoader;

namespace StickyMenu
{
    internal class Config
    {
        private readonly MelonPreferences_Category _generalCategory;
        public MelonPreferences_Entry<bool> Enabled;
        public MelonPreferences_Entry<bool> LockPosition;
        public MelonPreferences_Entry<bool> LockRotation;
        public MelonPreferences_Entry<bool> EnableDragging;

        public Config()
        {
            _generalCategory = MelonPreferences.CreateCategory("StickyMenu", "StickyMenu");
            _generalCategory.LoadFromFile();

            Enabled = _generalCategory.CreateEntry("Enabled", true);
            LockPosition = _generalCategory.CreateEntry("LockMenuPosition", true);
            LockRotation = _generalCategory.CreateEntry("LockMenuRotation", true);
            EnableDragging = _generalCategory.CreateEntry("EnableMenuDragging", true);

            _generalCategory.SaveToFile(false);
        }

        ~Config()
        {
            _generalCategory.SaveToFile();
        }
    }
}
