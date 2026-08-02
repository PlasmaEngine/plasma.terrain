// #pl-version 1

using PlasmaBuild.Core.Configuration;
using PlasmaBuild.Core.Rules;

public class PlasmaTerrainWorkspace : WorkspaceRules
{
    public PlasmaTerrainWorkspace(BuildContext context) : base(context)
    {
        TargetNames.Add("TerrainPlugin");
        TargetNames.Add("EditorPluginTerrain");
    }
}
