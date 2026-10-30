using PlasmaBuild.Core.Configuration;
using PlasmaBuild.Core.Rules;

public class EditorPluginDiversionModule : ModuleRules
{
    public EditorPluginDiversionModule(BuildContext context) : base(context)
    {
        PlasmaPackageSdk.ConfigurePackageModule(this, context, "Source/plEditorPluginDiversion",
            "EditorPluginDiversionPCH.h", "BUILDSYSTEM_BUILDING_EDITORPLUGINDIVERSION_LIB");

        // Editor-side headers and import libraries: EditorFramework, ToolsFoundation and the rest.
        // A runtime module must not take this - it would drag the editor into a shipped game.
        PlasmaPackageSdk.ConfigureEditorSdkConsumer(this, context);

        // The Version Control panel is engine-drawn UI: the editor's widgets and the UI elements they build.
        PlasmaPackageSdk.AddImportLibrary(this, context, "plEditorUi");
        PlasmaPackageSdk.AddImportLibrary(this, context, "plGameUiPlugin");
    }
}
