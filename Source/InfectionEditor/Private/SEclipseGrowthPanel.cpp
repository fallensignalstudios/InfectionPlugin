#include "SEclipseGrowthPanel.h"

#include "EclipseGrowthRunner.h"
#include "EclipseGrowthSettings.h"
#include "IDetailsView.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SEclipseGrowthPanel"

void SEclipseGrowthPanel::Construct(const FArguments& InArgs)
{
	UEclipseGrowthSettings* Settings = GetMutableDefault<UEclipseGrowthSettings>();

	FPropertyEditorModule& PropertyEditor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	FDetailsViewArgs Args;
	Args.bAllowSearch = true;
	Args.bHideSelectionTip = true;
	Args.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	Details = PropertyEditor.CreateDetailView(Args);
	Details->SetObject(Settings);
	Details->OnFinishedChangingProperties().AddLambda([Settings](const FPropertyChangedEvent&) { Settings->SaveConfig(); });

	Status = LOCTEXT("Ready",
		"Place an Eclipse Seed on a mesh (or use Add Seed), select the mesh, then Grow.");

	auto Button = [](const FText& Label, const FText& Tip, FOnClicked OnClicked) -> TSharedRef<SWidget>
	{
		return SNew(SButton)
			.Text(Label)
			.ToolTipText(Tip)
			.HAlign(HAlign_Center)
			.OnClicked(OnClicked);
	};

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(6.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(2.0f)
			[
				Button(LOCTEXT("Grow", "Grow on Selected"),
					LOCTEXT("GrowTip", "Grow corruption over each selected static mesh from the Eclipse Seeds near it."),
					FOnClicked::CreateSP(this, &SEclipseGrowthPanel::OnGrow))
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(2.0f)
			[
				Button(LOCTEXT("AddSeed", "Add Seed"),
					LOCTEXT("AddSeedTip", "Place an Eclipse Seed on top of each selected mesh."),
					FOnClicked::CreateSP(this, &SEclipseGrowthPanel::OnAddSeed))
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(2.0f)
			[
				Button(LOCTEXT("Materials", "Rebuild Materials"),
					LOCTEXT("MaterialsTip", "Regenerate M_EclipseBiomass and its instances from code, discarding edits to them."),
					FOnClicked::CreateSP(this, &SEclipseGrowthPanel::OnRebuildMaterials))
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(6.0f, 0.0f, 6.0f, 6.0f)
		[
			SNew(SBorder)
			.Padding(6.0f)
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.Text_Lambda([this] { return Status; })
			]
		]
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			Details.ToSharedRef()
		]
	];
}

FReply SEclipseGrowthPanel::OnGrow()
{
	Status = EclipseGrowthRunner::GrowOnSelection(*GetDefault<UEclipseGrowthSettings>());
	return FReply::Handled();
}

FReply SEclipseGrowthPanel::OnAddSeed()
{
	Status = EclipseGrowthRunner::AddSeedOnSelection();
	return FReply::Handled();
}

FReply SEclipseGrowthPanel::OnRebuildMaterials()
{
	Status = EclipseGrowthRunner::RebuildMaterials(*GetDefault<UEclipseGrowthSettings>());
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
