using System;
using MelonLoader;

namespace StickyMenu
{
    class Config
    {
        private MelonPreferences_Category generalCategory;
        public MelonPreferences_Entry<bool> enabled;
        /*public MelonPreferences_Entry<bool> constrainPosition;
        public MelonPreferences_Entry<bool> constrainRotation;*/

        public Config()
        {
            generalCategory = MelonPreferences.CreateCategory("StickyMenu", "StickyMenu");
            generalCategory.LoadFromFile();

            enabled = generalCategory.CreateEntry("Enabled", true);
            /*constrainPosition = generalCategory.CreateEntry("Constrain position", true);
            constrainRotation = generalCategory.CreateEntry("Constrain rotation", true);*/

            generalCategory.SaveToFile(false);
        }

        ~Config()
        {
            generalCategory.SaveToFile();
        }
    }
}
