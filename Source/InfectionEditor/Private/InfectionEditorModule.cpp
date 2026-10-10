#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "Modules/ModuleManager.h"
#include "SEclipseGrowthPanel.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"

#define LOCTEXT_NAMESPACE "InfectionEditor"

namespace
{
	const FName TabName(TEXT("EclipseGrowth"));
}

// Registers the Eclipse Growth tab and puts it in the Tools menu.
class FInfectionEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		FGlobalTabmanager::Get()->RegisterNomadTabSpawner(TabName,
			FOnSpawnTab::CreateLambda([](const FSpawnTabArgs&)
			{
				return SNew(SDockTab)
					.TabRole(ETabRole::NomadTab)
					[
						SNew(SEclipseGrowthPanel)
					];
			}))
			.SetDisplayName(LOCTEXT("TabTitle", "Eclipse Growth"))
			.SetTooltipText(LOCTEXT("TabTip", "Grow procedural Eclipse corruption over meshes in the level."))
			.SetMenuType(ETabSpawnerMenuType::Hidden);

		UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FInfectionEditorModule::RegisterMenus));
	}

	virtual void ShutdownModule() override
	{
		UToolMenus::UnRegisterStartupCallback(this);
		UToolMenus::UnregisterOwner(this);
		if (FSlateApplication::IsInitialized())
		{
			FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(TabName);
		}
	}

private:
	void RegisterMenus()
	{
		FToolMenuOwnerScoped OwnerScoped(this);
		UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Tools");
		FToolMenuSection& Section = Menu->FindOrAddSection("Eclipse", LOCTEXT("SectionLabel", "Eclipse"));
		Section.AddMenuEntry("OpenEclipseGrowth",
			LOCTEXT("MenuLabel", "Eclipse Growth"),
			LOCTEXT("MenuTip", "Open the Eclipse Growth panel."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([] { FGlobalTabmanager::Get()->TryInvokeTab(TabName); })));
	}
};

IMPLEMENT_MODULE(FInfectionEditorModule, InfectionEditor)

#undef LOCTEXT_NAMESPACE
