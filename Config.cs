using BepInEx.Configuration;

namespace StickyMenu
{
    public class Config
    {
        public ConfigEntry<bool> Enabled;
        public ConfigEntry<bool> LockPosition;
        public ConfigEntry<bool> LockRotation;
        public ConfigEntry<bool> EnableDragging;
        public ConfigEntry<bool> UseEdgeDragging;

        private ConfigFile _file;
        
        public Config(ConfigFile file)
        {
            _file = file;
            //_file.SaveOnConfigSet = true; // Don't need it before I have some way of changing settings from in-game

            Enabled = file.Bind("General", "Enabled", true, "Enable/disable mod");
            LockPosition = file.Bind("General", "LockMenuPosition", true, "Whether to lock the menus position to the player");
            LockRotation = file.Bind("General", "LockMenuRotation", true, "Whether to lock the menus rotation to the player");
            EnableDragging = file.Bind("General", "EnableMenuDragging", true, "Whether to enable dragging the menu by grabbing it");
            UseEdgeDragging = file.Bind("General" , "UseEdgeDragging", true, "Whether to drag by grabbing the edge. The alternative is using the JavaScript event");
        }

        ~Config()
        {
            
        }
    }
}
