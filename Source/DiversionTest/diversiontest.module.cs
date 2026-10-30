using System.IO;
using PlasmaBuild.Core.Configuration;
using PlasmaBuild.Core.Rules;

public class DiversionTestModule : ModuleRules
{
    public DiversionTestModule(BuildContext context) : base(context)
    {
        PlasmaPackageSdk.ConfigurePackageModule(this, context, "Source/DiversionTest", "DiversionTestPCH.h",
            "BUILDSYSTEM_BUILDING_DIVERSIONTEST");

        // test framework headers and import library from the SDK
        var unitTestsRoot = Path.Combine(PlasmaPackageSdk.Root, "Code", "UnitTests");
        PublicIncludePaths.Add(unitTestsRoot);
        PublicIncludePaths.Add(Path.Combine(unitTestsRoot, "TestFramework"));
        PublicIncludePaths.Add(Path.Combine(PlasmaPackageSdk.Root, "Intermediate", "PlasmaBuild", "Generated", "Code", "UnitTests"));
        PublicIncludePaths.Add(Path.Combine(PlasmaPackageSdk.Root, "Intermediate", "PlasmaBuild", "Generated", "Code", "UnitTests", "TestFramework"));

        if (context.Platform == TargetPlatform.Windows)
        {
            PublicIncludePaths.Add(Path.Combine(unitTestsRoot, "TestFramework", "Platform", "Win"));
        }

        PlasmaPackageSdk.AddImportLibrary(this, context, "plTestFramework");
    }
}
