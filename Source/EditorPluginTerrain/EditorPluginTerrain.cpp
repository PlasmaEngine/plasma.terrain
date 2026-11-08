#include <EditorPluginTerrain/EditorPluginTerrainPCH.h>

#include <EditorUi/Widgets.h>
#include <Foundation/Configuration/Plugin.h>

namespace
{
  /// The package's editor icons ("EditorIcons/TypeIcons/..." for ":/TypeIcons/..."): the plugin binary sits in
  /// <package>/Bin/<config>, both in the source tree and in an installed copy.
  plString GetIconFolder()
  {
    plStringBuilder sBinary, sCopy;
    plPlugin::GetPluginPaths("plEditorPluginTerrain", sBinary, sCopy, 0);

    plStringBuilder sFolder = sBinary.GetFileDirectory();
    sFolder.PathParentDirectory(2);
    sFolder.AppendPath("EditorIcons");
    return sFolder;
  }

  plString s_sIconFolder;
} // namespace

PL_PLUGIN_ON_LOADED()
{
  s_sIconFolder = GetIconFolder();
  plEditorUi::AddIconFolder(s_sIconFolder);
}

PL_PLUGIN_ON_UNLOADED()
{
  plEditorUi::RemoveIconFolder(s_sIconFolder);
  s_sIconFolder.Clear();
}
