using System.IO;
using PlasmaBuild.Core.Configuration;
using PlasmaBuild.Core.Rules;

public class TerrainPluginModule : ModuleRules
{
    public TerrainPluginModule(BuildContext context) : base(context)
    {
        PlasmaPackageSdk.ConfigurePackageModule(this, context, "Source/TerrainPlugin", "TerrainPluginPCH.h",
            "BUILDSYSTEM_BUILDING_TERRAINPLUGIN_LIB");

        // Shader headers shared with the engine's data directories; in the engine tree these were
        // ProjectRoot-relative, here they resolve against the SDK.
        PrivateIncludePaths.Add(Path.Combine(PlasmaPackageSdk.Root, "Data", "Base"));
        PrivateIncludePaths.Add(Path.Combine(PlasmaPackageSdk.Root, "Data", "Plugins"));
    }
}
