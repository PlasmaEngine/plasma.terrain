using PlasmaBuild.Core.Configuration;
using PlasmaBuild.Core.Rules;

public class TerrainPluginTarget : TargetRules
{
    public TerrainPluginTarget(BuildContext context) : base(context)
    {
        Type = TargetType.SharedLibrary;
        OutputName = "plTerrainPlugin";
        OutputDirectory = PlasmaPackageSdk.PackageBinaryDirectory(context);
        UsePCHFiles = true;
        UseUnityBuild = true;
        UseAdaptiveUnityBuild = true;
        UseIncrementalLinking = true;
        ExtraModules.Add("TerrainPluginModule");
    }
}
