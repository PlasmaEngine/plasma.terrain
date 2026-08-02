using PlasmaBuild.Core.Configuration;
using PlasmaBuild.Core.Rules;

public class EditorPluginTerrainModule : ModuleRules
{
    public EditorPluginTerrainModule(BuildContext context) : base(context)
    {
        PlasmaPackageSdk.ConfigurePackageModule(this, context, "Source/EditorPluginTerrain",
            "EditorPluginTerrainPCH.h", "BUILDSYSTEM_BUILDING_EDITORPLUGINTERRAIN_LIB");

        PlasmaPackageSdk.ConfigureEditorSdkConsumer(this, context);

        PlasmaPackageQt.ConfigureQtModule(this, context, "EditorPluginTerrainModule",
            "Source/EditorPluginTerrain", "Core", "Gui", "Widgets");
    }
}
