// #pl-version 1

using PlasmaBuild.Core.Configuration;
using PlasmaBuild.Core.Rules;

public class PlasmaDiversionWorkspace : WorkspaceRules
{
    public PlasmaDiversionWorkspace(BuildContext context) : base(context)
    {
        TargetNames.Add("EditorPluginDiversion");
        TargetNames.Add("EditorPluginDiversionMcp");
        TargetNames.Add("DiversionTest");
    }
}
