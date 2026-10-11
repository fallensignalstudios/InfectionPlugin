#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class IDetailsView;

// The Eclipse Growth tab: the settings, three actions and a status line.
class SEclipseGrowthPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SEclipseGrowthPanel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	FReply OnGrow();
	FReply OnAddSeed();
	FReply OnRemove();
	FReply OnVariation(int32 Step);
	FReply OnRebuildMaterials();

	TSharedPtr<IDetailsView> Details;
	FText Status;
};
