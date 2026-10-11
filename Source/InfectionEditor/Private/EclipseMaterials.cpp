#include "EclipseMaterials.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "EclipseMeshBridge.h"
#include "Engine/Texture2D.h"
#include "Factories/MaterialFactoryNew.h"
#include "Factories/MaterialInstanceConstantFactoryNew.h"
#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionCameraVectorWS.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionSubtract.h"
#include "Materials/MaterialExpressionDitherTemporalAA.h"
#include "Materials/MaterialExpressionLocalPosition.h"
#include "Materials/MaterialExpressionPreSkinnedNormal.h"
#include "Materials/MaterialExpressionPreSkinnedPosition.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionTextureObject.h"
#include "Materials/MaterialExpressionTextureObjectParameter.h"
#include "Materials/MaterialExpressionTime.h"
#include "Materials/MaterialExpressionTransform.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionVertexColor.h"
#include "Materials/MaterialExpressionVertexInterpolator.h"
#include "Materials/MaterialExpressionVertexNormalWS.h"
#include "Materials/MaterialExpressionWorldPosition.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Misc/FeedbackContext.h"
#include "UObject/Package.h"

#include "eclipse/detail.h"

#define LOCTEXT_NAMESPACE "EclipseMaterials"

namespace
{
	const TCHAR* BiomassName = TEXT("M_EclipseBiomass");
	const TCHAR* FleshName = TEXT("M_EclipseFlesh");
	const TCHAR* ChitinTextureName = TEXT("T_EclipseChitin");
	const TCHAR* FleshTextureName = TEXT("T_EclipseFlesh");
	const TCHAR* CapillaryTextureName = TEXT("T_EclipseCapillaries");
	const TCHAR* NoEventsTextureName = TEXT("T_EclipseNoEvents");
	// Matches UEclipseSpreadComponent::MaxBurns (and MaxOutbreaks): one row each.
	constexpr int32 EventSlots = 64;
	constexpr int32 DetailSize = 512;

	// Shared by every node: how far the growth has played (reveal) and how
	// overtaken this point is once revealed (s). UV.x is arrival: 0 at the
	// seed, 1 at the front. Animate Growth from 0 to about 1.4 to replay it.
	// EV comes from the Events node: x how much survives the burns (the band
	// where it is burning away glows as embers), y how far outbreaks have
	// revealed this point, z the glow of an outbreak's advancing front.
	// UV1.x marks soft tissue.
	const TCHAR* Prelude = TEXT(
		"float burn = EV.x;\n"
		"float grown = max(saturate((Growth - UV.x) / max(Softness, 0.001)), EV.y);\n"
		"float reveal = grown * burn;\n"
		"float s = saturate(VC.r) * reveal;\n"
		"float fleshy = saturate(UV1.x);\n"
		"float ember = 4.0 * burn * (1.0 - burn) * grown * step(0.001, VC.r + VC.g);\n");

	// Burns and outbreaks from the Eclipse Spread component, read from its
	// event texture: row 0 burns, row 1 outbreaks, each a centre relative to
	// EventOrigin (so they stay exact in a large world) and a radius. A burn
	// cuts the corruption out of a sphere, softened over BurnSoftness. An
	// outbreak reveals it inside a growing sphere; tissue that arrived later
	// in the grown spread lags behind its front, up to 1.5 x OutbreakRoughness,
	// so the front follows the corruption's own fingers. Must match
	// UEclipseSpreadComponent::GetInfectionAt.
	const TCHAR* EventsCode = TEXT(
		"float burn = 1.0;\n"
		"int burns = (int)BurnCount;\n"
		"[loop] for (int i = 0; i < burns; ++i)\n"
		"{\n"
		"    float4 b = Events.Load(int3(i, 0, 0));\n"
		"    if (b.w > 0.0) burn *= saturate((length(EP - b.xyz) - b.w) / max(BurnSoftness, 1.0));\n"
		"}\n"
		"float outbreak = 0.0, front = 0.0;\n"
		"int outbreaks = (int)OutbreakCount;\n"
		"float lag = min(UV.x, 1.5) * OutbreakRoughness;\n"
		"[loop] for (int j = 0; j < outbreaks; ++j)\n"
		"{\n"
		"    float4 o = Events.Load(int3(j, 1, 0));\n"
		"    float r = saturate((o.w - length(EP - o.xyz) - lag) / max(OutbreakSoftness, 1.0));\n"
		"    outbreak = max(outbreak, r);\n"
		"    front = max(front, 4.0 * r * (1.0 - r));\n"
		"}\n"
		"return float3(burn, outbreak, front);\n");

	// Triplanar detail over the pre-skinned position, so it stays put on an
	// animated character. PL is in centimetres; DetailScale is one tile. Hard
	// tissue samples the chitin map, soft tissue the finer flesh map. Leaves
	// d (the blended texel), dX/dY/dZ (each projection's), tn (the normal)
	// and tw (projection weights).
	const TCHAR* Triplanar = TEXT(
		"float3 tn = normalize(NL);\n"
		"float3 tw = pow(abs(tn), 4.0);\n"
		"tw /= tw.x + tw.y + tw.z;\n"
		"float3 tq = PL / max(DetailScale, 0.1);\n"
		"float3 fq = tq * 1.7;\n"
		"float4 dX = lerp(Texture2DSample(Chitin, ChitinSampler, tq.yz), Texture2DSample(FleshMap, FleshMapSampler, fq.yz), fleshy);\n"
		"float4 dY = lerp(Texture2DSample(Chitin, ChitinSampler, tq.xz), Texture2DSample(FleshMap, FleshMapSampler, fq.xz), fleshy);\n"
		"float4 dZ = lerp(Texture2DSample(Chitin, ChitinSampler, tq.xy), Texture2DSample(FleshMap, FleshMapSampler, fq.xy), fleshy);\n"
		"float4 d = dX * tw.x + dY * tw.y + dZ * tw.z;\n");

	// The maps store height slopes, not normals: tilting the surface normal
	// against the slope works in any projection and on any facing. Returns
	// the detailed normal in the mesh's own (pre-skinned) space.
	const TCHAR* DetailCode = TEXT(
		"float2 gX = dX.rg * 2.0 - 1.0;\n"
		"float2 gY = dY.rg * 2.0 - 1.0;\n"
		"float2 gZ = dZ.rg * 2.0 - 1.0;\n"
		"float3 grad = tw.x * float3(0.0, gX.x, gX.y) + tw.y * float3(gY.x, 0.0, gY.y) + tw.z * float3(gZ.x, gZ.y, 0.0);\n"
		"grad -= tn * dot(grad, tn);\n"
		"float strength = DetailStrength * lerp(1.0, FleshDetail, fleshy) * s;\n"
		"return normalize(tn - strength * grad);\n");

	// The detailed normal arrives through the component's rigid transform
	// (NPW) beside the plain normal taken the same way (NLW). Skinning turns
	// the plain normal into the vertex's world normal (NW); turn the detailed
	// one by the same rotation, so the detail follows a bending limb.
	const TCHAR* NormalCode = TEXT(
		"float3 a = normalize(NLW);\n"
		"float3 b = normalize(NW);\n"
		"float3 p = normalize(NPW);\n"
		"float c = dot(a, b);\n"
		"if (c < -0.999) return -p;\n"
		"float3 v = cross(a, b);\n"
		"return normalize(p * c + cross(v, p) + v * (dot(v, p) / (1.0 + c)));\n");

	// Carapace: near-black plates, raw tissue showing in the seams and on the
	// stretched flanks around growths, darkened down in the detail's grooves.
	const TCHAR* BaseColorCode = TEXT(
		"float inflamed = 4.0 * s * (1.0 - s);\n"
		"float3 host = lerp(HostColor, InflamedColor, OverlayMode);\n"
		"float3 c = lerp(host, InflamedColor, 0.45 * inflamed * (1.0 - s));\n"
		"float3 shell = lerp(CarapaceColor + CarapaceTint * VA, FleshColor, fleshy);\n"
		"c = lerp(c, shell, s);\n"
		"float bruise = saturate(-UV1.y), bone = saturate(UV1.y);\n"
		"c = lerp(c, BruiseColor, 0.85 * bruise * s);\n"
		"c = lerp(c, BoneColor, bone * s);\n"
		"float tint = OverlayMode > 0.5 ? step(0.001, HL.x) * (1.0 - s) : HL.x;\n"
		"c = lerp(c, HaloColor, tint);\n"
		"c = lerp(c, VesselColor, HL.y);\n"
		"return c * lerp(1.0, d.a, CavityStrength * s);\n");

	// The halo on clean tissue ahead of the front: x is how strongly it is
	// tinted, y the capillaries reaching out, denser near the front. Measured
	// from a little behind the front so it meets the corruption's soft edge,
	// and gone by HaloReach past it, where the overlay shell ends.
	const TCHAR* HaloCode = TEXT(
		"float3 tn = normalize(NL);\n"
		"float3 tw = pow(abs(tn), 4.0);\n"
		"tw /= tw.x + tw.y + tw.z;\n"
		"float3 vq = PL / max(CapillaryScale, 0.1);\n"
		"float vessel = Texture2DSample(Capillaries, CapillariesSampler, vq.yz).b * tw.x\n"
		"             + Texture2DSample(Capillaries, CapillariesSampler, vq.xz).b * tw.y\n"
		"             + Texture2DSample(Capillaries, CapillariesSampler, vq.xy).b * tw.z;\n"
		"float ahead = UV.x - (min(Growth, 1.0) - 0.15);\n"
		"float halo = ahead > 0.0 ? 1.0 - smoothstep(0.0, HaloReach + 0.15, ahead) : 0.0;\n"
		"halo *= (1.0 - s) * burn * (1.0 - IsGrowth) * saturate(Growth * 10.0);\n"
		"float lo = 0.95 - 0.3 * halo;\n"
		"return float2(HaloStrength * halo, smoothstep(lo, lo + 0.06, vessel) * sqrt(halo));\n");

	// The glow pulses in waves travelling outward from the seed. Exposed
	// tissue adds a faint scattered light, strongest at grazing angles.
	const TCHAR* EmissiveCode = TEXT(
		"float pulse = 1.0 + PulseAmount * sin(Time * PulseSpeed * 6.2831853 - UV.x * 8.0);\n"
		"float rim = pow(1.0 - saturate(abs(dot(normalize(NW), normalize(V)))), 3.0);\n"
		"float3 scatter = ScatterColor * FleshGlow * fleshy * s * (0.3 + rim);\n"
		"float frontGlow = 0.35 * EV.z * burn * step(0.001, VC.r + VC.g);\n"
		"return GlowIntensity * (GlowColor * (saturate(VC.g) * reveal * pulse + frontGlow) + EmberColor * ember) + scatter;\n");

	const TCHAR* RoughnessCode = TEXT(
		"float bone = saturate(UV1.y);\n"
		"return lerp(lerp(lerp(0.7, 0.22, s), FleshRoughness, fleshy * s), 0.42, bone * s);\n");
	const TCHAR* ClearCoatCode = TEXT("return s * (1.0 - 0.6 * fleshy) * (1.0 - 0.6 * saturate(UV1.y));\n");

	// Overlay shells show only where corrupted, on a vein's bright core or in
	// the halo; growths show once revealed; a replacing surface is always
	// solid. The result is dithered, so the halo's partial tint reads as a
	// soft wash over the host under temporal anti-aliasing.
	const TCHAR* OpacityCode = TEXT(
		"float visible = saturate(max(VC.r * 2.0, (VC.g - 0.3) * 3.0));\n"
		"float surfaceOpacity = lerp(1.0, max(reveal * visible, max(HL.x, HL.y)), OverlayMode);\n"
		"return lerp(surfaceOpacity, reveal, IsGrowth);\n");

	// Height of the carapace detail for Nanite tessellation, in the mesh's
	// own space; only where the corruption has grown.
	const TCHAR* DisplacementCode = TEXT("return d.b * s * (1.0 - 0.5 * fleshy);\n");

	// Soft tissue: dark wine flesh, lighter on its swellings and black in its
	// folds. The dark lid around an eye arrives in the crack channel.
	const TCHAR* FleshBaseCode = TEXT(
		"float3 c = lerp(FleshDeep, FleshColor, saturate(0.4 + d.b));\n"
		"c *= lerp(1.0, d.a, CavityStrength);\n"
		"c *= 1.0 - 0.5 * saturate(-UV1.y);\n"
		"return c * (1.0 - 0.85 * saturate(VC.b));\n");
	const TCHAR* FleshRoughnessCode = TEXT("return FleshRoughness + 0.25 * (1.0 - d.a);\n");
	const TCHAR* FleshOpacityCode = TEXT("return reveal;\n");

	FString ObjectPath(const FString& PackageName, const FString& Name)
	{
		return PackageName + TEXT(".") + Name;
	}

	template <class T>
	T* LoadQuietly(const FString& PackageName, const FString& Name)
	{
		return LoadObject<T>(nullptr, *ObjectPath(PackageName, Name), nullptr, LOAD_NoWarn | LOAD_Quiet);
	}

	UMaterialExpressionScalarParameter* Scalar(UMaterial* M, const TCHAR* Name, float Value, int32 Y,
		const TCHAR* Group = TEXT("Eclipse"))
	{
		auto* P = Cast<UMaterialExpressionScalarParameter>(
			UMaterialEditingLibrary::CreateMaterialExpression(M, UMaterialExpressionScalarParameter::StaticClass(), -900, Y));
		P->ParameterName = Name;
		P->DefaultValue = Value;
		P->Group = Group;
		return P;
	}

	UMaterialExpressionVectorParameter* Colour(UMaterial* M, const TCHAR* Name, const FLinearColor& Value, int32 Y)
	{
		auto* P = Cast<UMaterialExpressionVectorParameter>(
			UMaterialEditingLibrary::CreateMaterialExpression(M, UMaterialExpressionVectorParameter::StaticClass(), -1200, Y));
		P->ParameterName = Name;
		P->DefaultValue = Value;
		P->Group = TEXT("Eclipse Colours");
		return P;
	}

	struct FInputs
	{
		TArray<TPair<FName, TPair<UMaterialExpression*, FString>>> List;  // input name -> (expression, its output)
		void Add(const TCHAR* Name, UMaterialExpression* From, const TCHAR* Output = TEXT(""))
		{
			List.Emplace(FName(Name), MakeTuple(From, FString(Output)));
		}
	};

	// A Custom node running Body (after the Prelude, unless bPrelude is
	// false) over the named inputs.
	UMaterialExpressionCustom* Custom(UMaterial* M, const TCHAR* Description, const FString& Body,
		ECustomMaterialOutputType Type, const FInputs& Inputs, int32 Y, bool bPrelude = true)
	{
		auto* C = Cast<UMaterialExpressionCustom>(
			UMaterialEditingLibrary::CreateMaterialExpression(M, UMaterialExpressionCustom::StaticClass(), -350, Y));
		C->Description = Description;
		C->Code = bPrelude ? FString(Prelude) + Body : Body;
		C->OutputType = Type;
		C->Inputs.Reset();
		for (const auto& Input : Inputs.List)
		{
			FCustomInput Slot;
			Slot.InputName = Input.Key;
			C->Inputs.Add(Slot);
		}
		for (const auto& Input : Inputs.List)
		{
			UMaterialEditingLibrary::ConnectMaterialExpressions(Input.Value.Key, Input.Value.Value, C, Input.Key.ToString());
		}
		return C;
	}

	// A pre-skinned (bind pose) attribute, passed on from the vertex shader:
	// the engine provides it only there.
	UMaterialExpression* Interpolated(UMaterial* M, UClass* Attribute, int32 Y)
	{
		UMaterialExpression* Source = UMaterialEditingLibrary::CreateMaterialExpression(M, Attribute, -1500, Y);
		auto* Interpolator = Cast<UMaterialExpressionVertexInterpolator>(
			UMaterialEditingLibrary::CreateMaterialExpression(M, UMaterialExpressionVertexInterpolator::StaticClass(), -1200, Y));
		Interpolator->Input.Expression = Source;
		return Interpolator;
	}

	UMaterialExpressionTransform* LocalToWorld(UMaterial* M, UMaterialExpression* Input, int32 Y)
	{
		auto* T = Cast<UMaterialExpressionTransform>(
			UMaterialEditingLibrary::CreateMaterialExpression(M, UMaterialExpressionTransform::StaticClass(), -100, Y));
		T->Input.Expression = Input;
		T->TransformSourceType = TRANSFORMSOURCE_Local;
		T->TransformType = TRANSFORM_World;
		return T;
	}

	UMaterialExpressionTextureObject* TextureObject(UMaterial* M, UTexture2D* Texture, int32 Y)
	{
		auto* T = Cast<UMaterialExpressionTextureObject>(
			UMaterialEditingLibrary::CreateMaterialExpression(M, UMaterialExpressionTextureObject::StaticClass(), -1500, Y));
		T->Texture = Texture;
		T->AutoSetSampleType();
		return T;
	}

	// One of the generated detail maps (see eclipse/detail.h), stored
	// uncompressed: its channels are slopes and heights, not colours.
	UTexture2D* BuildDetailTexture(const FString& Folder, const TCHAR* Name, eclipse::DetailKind Kind,
		UTexture2D* Existing)
	{
		const eclipse::DetailMap Map = eclipse::makeDetailMap(Kind, DetailSize);
		TArray<uint8> Bgra;
		Bgra.SetNumUninitialized(static_cast<int32>(Map.rgba.size()));
		for (int32 I = 0; I + 3 < Bgra.Num(); I += 4)
		{
			Bgra[I + 0] = Map.rgba[I + 2];
			Bgra[I + 1] = Map.rgba[I + 1];
			Bgra[I + 2] = Map.rgba[I + 0];
			Bgra[I + 3] = Map.rgba[I + 3];
		}
		UTexture2D* Texture = Existing;
		if (Texture)
		{
			Texture->Modify();
		}
		else
		{
			UPackage* Package = CreatePackage(*(Folder / Name));
			Texture = NewObject<UTexture2D>(Package, Name, RF_Public | RF_Standalone | RF_Transactional);
			FAssetRegistryModule::AssetCreated(Texture);
		}
		Texture->Source.Init(Map.size, Map.size, 1, 1, TSF_BGRA8, Bgra.GetData());
		Texture->SRGB = false;
		Texture->CompressionSettings = TC_VectorDisplacementmap;  // uncompressed 8-bit RGBA
		Texture->AddressX = TA_Wrap;
		Texture->AddressY = TA_Wrap;
		Texture->PostEditChange();
		Texture->MarkPackageDirty();
		return Texture;
	}

	UMaterial* CreateOrClear(const FString& Folder, const TCHAR* Name, UMaterial* Existing, FText& OutError)
	{
		if (Existing)
		{
			Existing->Modify();
			UMaterialEditingLibrary::DeleteAllMaterialExpressions(Existing);
			return Existing;
		}
		UPackage* Package = CreatePackage(*(Folder / Name));
		auto* Factory = NewObject<UMaterialFactoryNew>();
		UMaterial* M = Cast<UMaterial>(Factory->FactoryCreateNew(UMaterial::StaticClass(), Package, Name,
			RF_Public | RF_Standalone | RF_Transactional, nullptr, GWarn));
		if (!M)
		{
			OutError = FText::Format(LOCTEXT("MaterialFailed", "Could not create {0}."), FText::FromString(Name));
			return nullptr;
		}
		FAssetRegistryModule::AssetCreated(M);
		return M;
	}

	struct FDetailTextures
	{
		UTexture2D* Chitin = nullptr;
		UTexture2D* Flesh = nullptr;
		UTexture2D* Capillaries = nullptr;
		UTexture2D* NoEvents = nullptr;  // default for the Events parameter: no burns, no outbreaks
	};

	// An all-zero event texture, the shape the spread component writes.
	UTexture2D* BuildNoEventsTexture(const FString& Folder, UTexture2D* Existing)
	{
		TArray<uint8> Zero;
		Zero.SetNumZeroed(EventSlots * 2 * 4);
		UTexture2D* Texture = Existing;
		if (Texture)
		{
			Texture->Modify();
		}
		else
		{
			UPackage* Package = CreatePackage(*(Folder / NoEventsTextureName));
			Texture = NewObject<UTexture2D>(Package, NoEventsTextureName, RF_Public | RF_Standalone | RF_Transactional);
			FAssetRegistryModule::AssetCreated(Texture);
		}
		Texture->Source.Init(EventSlots, 2, 1, 1, TSF_BGRA8, Zero.GetData());
		Texture->SRGB = false;
		Texture->CompressionSettings = TC_VectorDisplacementmap;
		Texture->Filter = TF_Nearest;
		Texture->MipGenSettings = TMGS_NoMipmaps;
		Texture->PostEditChange();
		Texture->MarkPackageDirty();
		return Texture;
	}

	// Sets a float inside a struct UPROPERTY by name, for engine settings
	// reached through reflection (see EclipseBridge::SetBoolProperty).
	bool SetStructFloat(UObject* Object, FName StructProperty, FName Field, float Value)
	{
		FStructProperty* Outer = FindFProperty<FStructProperty>(Object->GetClass(), StructProperty);
		FFloatProperty* Inner = Outer ? FindFProperty<FFloatProperty>(Outer->Struct, Field) : nullptr;
		if (!Inner)
		{
			return false;
		}
		Inner->SetPropertyValue_InContainer(Outer->ContainerPtrToValuePtr<void>(Object), Value);
		return true;
	}

	// Builds a master into Existing (cleared first) or into a new asset. Both
	// masters share the reveal, burn, glow and detail logic; the carapace one
	// is clear-coated, the soft-tissue one scatters light through itself.
	UMaterial* BuildMaster(const FString& Folder, bool bFlesh, UMaterial* Existing, const FDetailTextures& Detail,
		const FEclipseMaterialOptions& Options, FText& OutError)
	{
		UMaterial* M = CreateOrClear(Folder, bFlesh ? FleshName : BiomassName, Existing, OutError);
		if (!M)
		{
			return nullptr;
		}

		// Masked so shells and growths can be revealed; two-sided for
		// membranes and open environment shells. The normal is given in world
		// space (see NormalCode), so it needs no tangents from the mesh.
		EclipseBridge::SetEnumProperty(M, TEXT("BlendMode"), BLEND_Masked);
		EclipseBridge::SetBoolProperty(M, TEXT("TwoSided"), true);
		EclipseBridge::SetBoolProperty(M, TEXT("bTangentSpaceNormal"), false);
		M->SetShadingModel(bFlesh ? MSM_Subsurface : MSM_ClearCoat);

		auto* Vertex = UMaterialEditingLibrary::CreateMaterialExpression(M, UMaterialExpressionVertexColor::StaticClass(), -900, -900);
		auto* Coords = Cast<UMaterialExpressionTextureCoordinate>(
			UMaterialEditingLibrary::CreateMaterialExpression(M, UMaterialExpressionTextureCoordinate::StaticClass(), -900, -760));
		Coords->CoordinateIndex = 0;
		auto* Coords1 = Cast<UMaterialExpressionTextureCoordinate>(
			UMaterialEditingLibrary::CreateMaterialExpression(M, UMaterialExpressionTextureCoordinate::StaticClass(), -900, -680));
		Coords1->CoordinateIndex = 1;
		auto* Time = UMaterialEditingLibrary::CreateMaterialExpression(M, UMaterialExpressionTime::StaticClass(), -900, -600);
		auto* VertexNormal = UMaterialEditingLibrary::CreateMaterialExpression(M, UMaterialExpressionVertexNormalWS::StaticClass(), -900, -540);
		auto* Camera = UMaterialEditingLibrary::CreateMaterialExpression(M, UMaterialExpressionCameraVectorWS::StaticClass(), -900, -480);
		UMaterialExpression* LocalPosition = Interpolated(M, UMaterialExpressionPreSkinnedPosition::StaticClass(), -1100);
		UMaterialExpression* LocalNormal = Interpolated(M, UMaterialExpressionPreSkinnedNormal::StaticClass(), -1000);
		auto* ChitinMap = TextureObject(M, Detail.Chitin, -1300);
		auto* FleshMap = TextureObject(M, Detail.Flesh, -1450);

		auto* Growth = Scalar(M, TEXT("Growth"), 4.0f, -380);
		auto* Softness = Scalar(M, TEXT("Softness"), 0.08f, -300);
		auto* GlowIntensity = Scalar(M, TEXT("GlowIntensity"), 12.0f, -220);
		auto* PulseSpeed = Scalar(M, TEXT("PulseSpeed"), 0.6f, -140);
		auto* PulseAmount = Scalar(M, TEXT("PulseAmount"), 0.35f, -60);
		auto* BurnSoftness = Scalar(M, TEXT("BurnSoftness"), 40.0f, 260);

		auto* DetailScale = Scalar(M, TEXT("DetailScale"), 12.0f, 900, TEXT("Eclipse Detail"));
		auto* DetailStrength = Scalar(M, TEXT("DetailStrength"), 0.35f, 980, TEXT("Eclipse Detail"));
		auto* FleshDetail = Scalar(M, TEXT("FleshDetail"), 1.3f, 1060, TEXT("Eclipse Detail"));
		auto* CavityStrength = Scalar(M, TEXT("CavityStrength"), 0.6f, 1140, TEXT("Eclipse Detail"));
		auto* FleshRoughness = Scalar(M, TEXT("FleshRoughness"), 0.32f, 1220, TEXT("Eclipse Flesh"));
		auto* FleshGlow = Scalar(M, TEXT("FleshGlow"), 0.15f, 1300, TEXT("Eclipse Flesh"));

		auto* GlowColor = Colour(M, TEXT("GlowColor"), FLinearColor(0.55f, 0.14f, 1.0f), -140);
		auto* EmberColor = Colour(M, TEXT("EmberColor"), FLinearColor(1.0f, 0.32f, 0.06f), 0);
		auto* FleshColor = Colour(M, TEXT("FleshColor"), FLinearColor(0.10f, 0.025f, 0.05f), 140);
		auto* ScatterColor = Colour(M, TEXT("ScatterColor"), FLinearColor(0.55f, 0.04f, 0.2f), 420);

		FInputs Common;
		Common.Add(TEXT("VC"), Vertex);
		Common.Add(TEXT("VA"), Vertex, TEXT("A"));
		Common.Add(TEXT("UV"), Coords);
		Common.Add(TEXT("UV1"), Coords1);
		Common.Add(TEXT("Growth"), Growth);
		Common.Add(TEXT("Softness"), Softness);

		// Burns and outbreaks, set by the Eclipse Spread component at runtime.
		// The world position minus EventOrigin is taken in the engine's
		// large-world precision; what reaches the Custom node is small.
		auto* World = UMaterialEditingLibrary::CreateMaterialExpression(M, UMaterialExpressionWorldPosition::StaticClass(), -1500, 400);
		auto* EventOrigin = Colour(M, TEXT("EventOrigin"), FLinearColor(0.0f, 0.0f, 0.0f, 0.0f), 560);
		EventOrigin->Group = TEXT("Eclipse Events");
		auto* Relative = UMaterialEditingLibrary::CreateMaterialExpression(M, UMaterialExpressionSubtract::StaticClass(), -1200, 450);
		UMaterialEditingLibrary::ConnectMaterialExpressions(World, TEXT(""), Relative, TEXT("A"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(EventOrigin, TEXT(""), Relative, TEXT("B"));
		auto* Events = Cast<UMaterialExpressionTextureObjectParameter>(
			UMaterialEditingLibrary::CreateMaterialExpression(M, UMaterialExpressionTextureObjectParameter::StaticClass(), -1500, 700));
		Events->ParameterName = TEXT("Events");
		Events->Group = TEXT("Eclipse Events");
		Events->Texture = Detail.NoEvents;
		Events->AutoSetSampleType();
		auto* BurnCount = Scalar(M, TEXT("BurnCount"), 0.0f, 620, TEXT("Eclipse Events"));
		auto* OutbreakCount = Scalar(M, TEXT("OutbreakCount"), 0.0f, 700, TEXT("Eclipse Events"));
		auto* OutbreakSoftness = Scalar(M, TEXT("OutbreakSoftness"), 30.0f, 780, TEXT("Eclipse Events"));
		auto* OutbreakRoughness = Scalar(M, TEXT("OutbreakRoughness"), 60.0f, 860, TEXT("Eclipse Events"));
		FInputs EventInputs;
		EventInputs.Add(TEXT("EP"), Relative);
		EventInputs.Add(TEXT("UV"), Coords);
		EventInputs.Add(TEXT("Events"), Events);
		EventInputs.Add(TEXT("BurnCount"), BurnCount);
		EventInputs.Add(TEXT("OutbreakCount"), OutbreakCount);
		EventInputs.Add(TEXT("BurnSoftness"), BurnSoftness);
		EventInputs.Add(TEXT("OutbreakSoftness"), OutbreakSoftness);
		EventInputs.Add(TEXT("OutbreakRoughness"), OutbreakRoughness);
		auto* EventsNode = Custom(M, TEXT("Eclipse Events"), EventsCode, CMOT_Float3, EventInputs, 450, /*bPrelude*/ false);
		Common.Add(TEXT("EV"), EventsNode);

		FInputs DetailInputs = Common;
		DetailInputs.Add(TEXT("PL"), LocalPosition);
		DetailInputs.Add(TEXT("NL"), LocalNormal);
		DetailInputs.Add(TEXT("Chitin"), ChitinMap);
		DetailInputs.Add(TEXT("FleshMap"), FleshMap);
		DetailInputs.Add(TEXT("DetailScale"), DetailScale);
		DetailInputs.Add(TEXT("CavityStrength"), CavityStrength);

		// Normal: the detail in the mesh's own space, then into the world.
		FInputs DetailNormalInputs = DetailInputs;
		DetailNormalInputs.Add(TEXT("DetailStrength"), DetailStrength);
		DetailNormalInputs.Add(TEXT("FleshDetail"), FleshDetail);
		auto* DetailNode = Custom(M, TEXT("Eclipse Detail"), FString(Triplanar) + DetailCode, CMOT_Float3, DetailNormalInputs, 700);
		FInputs NormalInputs;
		NormalInputs.Add(TEXT("NPW"), LocalToWorld(M, DetailNode, 700));
		NormalInputs.Add(TEXT("NLW"), LocalToWorld(M, LocalNormal, 800));
		NormalInputs.Add(TEXT("NW"), VertexNormal);
		auto* NormalNode = Custom(M, TEXT("Eclipse Normal"), NormalCode, CMOT_Float3, NormalInputs, 750, /*bPrelude*/ false);
		NormalNode->MaterialExpressionEditorX = 150;

		FInputs EmissiveInputs = Common;
		EmissiveInputs.Add(TEXT("Time"), Time);
		EmissiveInputs.Add(TEXT("GlowColor"), GlowColor);
		EmissiveInputs.Add(TEXT("EmberColor"), EmberColor);
		EmissiveInputs.Add(TEXT("GlowIntensity"), GlowIntensity);
		EmissiveInputs.Add(TEXT("PulseSpeed"), PulseSpeed);
		EmissiveInputs.Add(TEXT("PulseAmount"), PulseAmount);
		EmissiveInputs.Add(TEXT("ScatterColor"), ScatterColor);
		EmissiveInputs.Add(TEXT("FleshGlow"), FleshGlow);
		EmissiveInputs.Add(TEXT("NW"), VertexNormal);
		EmissiveInputs.Add(TEXT("V"), Camera);
		auto* EmissiveNode = Custom(M, TEXT("Eclipse Glow"), EmissiveCode, CMOT_Float3, EmissiveInputs, -350);

		if (!bFlesh)
		{
			auto* OverlayMode = Scalar(M, TEXT("OverlayMode"), 1.0f, 20);
			auto* IsGrowth = Scalar(M, TEXT("IsGrowth"), 0.0f, 100);
			auto* CoatRoughness = Scalar(M, TEXT("ClearCoatRoughness"), 0.06f, 180);
			auto* HostColor = Colour(M, TEXT("HostColor"), FLinearColor(0.50f, 0.43f, 0.41f), -700);
			auto* InflamedColor = Colour(M, TEXT("InflamedColor"), FLinearColor(0.12f, 0.05f, 0.10f), -560);
			auto* CarapaceColor = Colour(M, TEXT("CarapaceColor"), FLinearColor(0.018f, 0.015f, 0.022f), -420);
			auto* CarapaceTint = Colour(M, TEXT("CarapaceTint"), FLinearColor(0.02f, 0.008f, 0.03f), -280);

			auto* CapillaryMap = TextureObject(M, Detail.Capillaries, -1600);
			auto* CapillaryScale = Scalar(M, TEXT("CapillaryScale"), 10.0f, 1460, TEXT("Eclipse Halo"));
			auto* HaloReach = Scalar(M, TEXT("HaloReach"), Options.HaloReach, 1540, TEXT("Eclipse Halo"));
			auto* HaloStrength = Scalar(M, TEXT("HaloStrength"), 0.6f, 1620, TEXT("Eclipse Halo"));
			auto* HaloColor = Colour(M, TEXT("HaloColor"), FLinearColor(0.24f, 0.15f, 0.19f), 1460);
			auto* VesselColor = Colour(M, TEXT("VesselColor"), FLinearColor(0.07f, 0.015f, 0.05f), 1600);
			auto* BruiseColor = Colour(M, TEXT("BruiseColor"), FLinearColor(0.05f, 0.008f, 0.028f), 1740);
			auto* BoneColor = Colour(M, TEXT("BoneColor"), FLinearColor(0.33f, 0.29f, 0.32f), 1880);

			FInputs HaloInputs = Common;
			HaloInputs.Add(TEXT("PL"), LocalPosition);
			HaloInputs.Add(TEXT("NL"), LocalNormal);
			HaloInputs.Add(TEXT("Capillaries"), CapillaryMap);
			HaloInputs.Add(TEXT("CapillaryScale"), CapillaryScale);
			HaloInputs.Add(TEXT("HaloReach"), HaloReach);
			HaloInputs.Add(TEXT("HaloStrength"), HaloStrength);
			HaloInputs.Add(TEXT("IsGrowth"), IsGrowth);
			auto* HaloNode = Custom(M, TEXT("Eclipse Halo"), HaloCode, CMOT_Float2, HaloInputs, 1000);

			FInputs BaseInputs = DetailInputs;
			BaseInputs.Add(TEXT("HL"), HaloNode);
			BaseInputs.Add(TEXT("HaloColor"), HaloColor);
			BaseInputs.Add(TEXT("VesselColor"), VesselColor);
			BaseInputs.Add(TEXT("BruiseColor"), BruiseColor);
			BaseInputs.Add(TEXT("BoneColor"), BoneColor);
			BaseInputs.Add(TEXT("HostColor"), HostColor);
			BaseInputs.Add(TEXT("InflamedColor"), InflamedColor);
			BaseInputs.Add(TEXT("CarapaceColor"), CarapaceColor);
			BaseInputs.Add(TEXT("CarapaceTint"), CarapaceTint);
			BaseInputs.Add(TEXT("FleshColor"), FleshColor);
			BaseInputs.Add(TEXT("OverlayMode"), OverlayMode);

			FInputs RoughInputs = Common;
			RoughInputs.Add(TEXT("FleshRoughness"), FleshRoughness);

			FInputs OpacityInputs = Common;
			OpacityInputs.Add(TEXT("OverlayMode"), OverlayMode);
			OpacityInputs.Add(TEXT("IsGrowth"), IsGrowth);
			OpacityInputs.Add(TEXT("HL"), HaloNode);

			auto* BaseNode = Custom(M, TEXT("Eclipse Base Colour"), FString(Triplanar) + BaseColorCode, CMOT_Float3, BaseInputs, -600);
			auto* RoughNode = Custom(M, TEXT("Eclipse Roughness"), RoughnessCode, CMOT_Float1, RoughInputs, -150);
			auto* CoatNode = Custom(M, TEXT("Eclipse Clear Coat"), ClearCoatCode, CMOT_Float1, Common, 0);
			auto* OpacityNode = Custom(M, TEXT("Eclipse Opacity"), OpacityCode, CMOT_Float1, OpacityInputs, 150);

			UMaterialEditingLibrary::ConnectMaterialProperty(BaseNode, TEXT(""), MP_BaseColor);
			UMaterialEditingLibrary::ConnectMaterialProperty(RoughNode, TEXT(""), MP_Roughness);
			UMaterialEditingLibrary::ConnectMaterialProperty(CoatNode, TEXT(""), MP_CustomData0);       // clear coat
			UMaterialEditingLibrary::ConnectMaterialProperty(CoatRoughness, TEXT(""), MP_CustomData1);  // clear coat roughness
			auto* Dither = Cast<UMaterialExpressionDitherTemporalAA>(
				UMaterialEditingLibrary::CreateMaterialExpression(M, UMaterialExpressionDitherTemporalAA::StaticClass(), 0, 150));
			Dither->AlphaThreshold.Expression = OpacityNode;
			UMaterialEditingLibrary::ConnectMaterialProperty(Dither, TEXT(""), MP_OpacityMask);

			if (Options.bDisplacement)
			{
				// Evaluated where Nanite tessellates, so straight from the
				// local position rather than through a vertex interpolator.
				FInputs DisplacementInputs = DetailInputs;
				auto* Local = UMaterialEditingLibrary::CreateMaterialExpression(M, UMaterialExpressionLocalPosition::StaticClass(), -1500, 2000);
				for (auto& Input : DisplacementInputs.List)
				{
					if (Input.Key == TEXT("PL"))
					{
						Input.Value = MakeTuple(Local, FString());
					}
					else if (Input.Key == TEXT("NL"))
					{
						Input.Value = MakeTuple(static_cast<UMaterialExpression*>(VertexNormal), FString());
					}
				}
				auto* DisplacementNode = Custom(M, TEXT("Eclipse Displacement"), FString(Triplanar) + DisplacementCode,
					CMOT_Float1, DisplacementInputs, 2000);
				UMaterialEditingLibrary::ConnectMaterialProperty(DisplacementNode, TEXT(""), MP_Displacement);
				const bool bTessellation = EclipseBridge::SetBoolProperty(M, TEXT("bEnableTessellation"), true);
				const bool bScaled = SetStructFloat(M, TEXT("DisplacementScaling"), TEXT("Magnitude"), Options.DisplacementDepth) &&
									 SetStructFloat(M, TEXT("DisplacementScaling"), TEXT("Center"), 0.0f);
				if (!bTessellation || !bScaled)
				{
					UE_LOG(LogTemp, Warning,
						TEXT("Eclipse: could not enable tessellation on %s by property name; enable it in the material's Nanite settings."),
						*M->GetName());
				}
			}
		}
		else
		{
			auto* Translucency = Scalar(M, TEXT("Translucency"), 0.65f, 1380, TEXT("Eclipse Flesh"));
			auto* FleshDeep = Colour(M, TEXT("FleshDeep"), FLinearColor(0.035f, 0.008f, 0.02f), 280);

			FInputs BaseInputs = DetailInputs;
			BaseInputs.Add(TEXT("FleshColor"), FleshColor);
			BaseInputs.Add(TEXT("FleshDeep"), FleshDeep);
			FInputs RoughInputs = DetailInputs;
			RoughInputs.Add(TEXT("FleshRoughness"), FleshRoughness);

			auto* BaseNode = Custom(M, TEXT("Eclipse Flesh Colour"), FString(Triplanar) + FleshBaseCode, CMOT_Float3, BaseInputs, -600);
			auto* RoughNode = Custom(M, TEXT("Eclipse Flesh Roughness"), FString(Triplanar) + FleshRoughnessCode, CMOT_Float1,
				RoughInputs, -150);
			auto* OpacityNode = Custom(M, TEXT("Eclipse Opacity"), FleshOpacityCode, CMOT_Float1, Common, 150);

			UMaterialEditingLibrary::ConnectMaterialProperty(BaseNode, TEXT(""), MP_BaseColor);
			UMaterialEditingLibrary::ConnectMaterialProperty(RoughNode, TEXT(""), MP_Roughness);
			UMaterialEditingLibrary::ConnectMaterialProperty(ScatterColor, TEXT(""), MP_SubsurfaceColor);
			UMaterialEditingLibrary::ConnectMaterialProperty(Translucency, TEXT(""), MP_Opacity);  // how much light passes through
			UMaterialEditingLibrary::ConnectMaterialProperty(OpacityNode, TEXT(""), MP_OpacityMask);
		}
		UMaterialEditingLibrary::ConnectMaterialProperty(EmissiveNode, TEXT(""), MP_EmissiveColor);
		UMaterialEditingLibrary::ConnectMaterialProperty(NormalNode, TEXT(""), MP_Normal);

		UMaterialEditingLibrary::RecompileMaterial(M);
		M->MarkPackageDirty();
		return M;
	}

	// Sets the instance's switches on Existing, or on a new asset.
	UMaterialInterface* BuildInstance(const FString& Folder, const TCHAR* Name, UMaterial* Parent,
		UMaterialInstanceConstant* Existing, const TArray<TPair<FName, float>>& Scalars)
	{
		UMaterialInstanceConstant* Instance = Existing;
		if (Instance)
		{
			Instance->Modify();
			Instance->SetParentEditorOnly(Parent);
		}
		else
		{
			UPackage* Package = CreatePackage(*(Folder / Name));
			auto* Factory = NewObject<UMaterialInstanceConstantFactoryNew>();
			Factory->InitialParent = Parent;
			Instance = Cast<UMaterialInstanceConstant>(Factory->FactoryCreateNew(UMaterialInstanceConstant::StaticClass(),
				Package, Name, RF_Public | RF_Standalone | RF_Transactional, nullptr, GWarn));
			if (!Instance)
			{
				return nullptr;
			}
			FAssetRegistryModule::AssetCreated(Instance);
		}
		for (const auto& Pair : Scalars)
		{
			Instance->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(Pair.Key), Pair.Value);
		}
		Instance->PostEditChange();
		Instance->MarkPackageDirty();
		return Instance;
	}
}

bool FEclipseMaterials::Ensure(const FString& OutputFolder, bool bRebuild, FEclipseMaterials& Out, FText& OutError,
	const FEclipseMaterialOptions& Options)
{
	const FString Folder = OutputFolder / TEXT("Materials");

	FDetailTextures Detail;
	Detail.Chitin = LoadQuietly<UTexture2D>(Folder / ChitinTextureName, ChitinTextureName);
	Detail.Flesh = LoadQuietly<UTexture2D>(Folder / FleshTextureName, FleshTextureName);
	Detail.Capillaries = LoadQuietly<UTexture2D>(Folder / CapillaryTextureName, CapillaryTextureName);
	Detail.NoEvents = LoadQuietly<UTexture2D>(Folder / NoEventsTextureName, NoEventsTextureName);
	if (!Detail.NoEvents || bRebuild)
	{
		Detail.NoEvents = BuildNoEventsTexture(Folder, Detail.NoEvents);
	}
	if (!Detail.Capillaries || bRebuild)
	{
		Detail.Capillaries = BuildDetailTexture(Folder, CapillaryTextureName, eclipse::DetailKind::Capillaries, Detail.Capillaries);
	}
	if (!Detail.Chitin || bRebuild)
	{
		Detail.Chitin = BuildDetailTexture(Folder, ChitinTextureName, eclipse::DetailKind::Chitin, Detail.Chitin);
	}
	if (!Detail.Flesh || bRebuild)
	{
		Detail.Flesh = BuildDetailTexture(Folder, FleshTextureName, eclipse::DetailKind::Flesh, Detail.Flesh);
	}

	// 0: carapace (M_EclipseBiomass), 1: soft tissue (M_EclipseFlesh).
	UMaterial* Masters[2] = {};
	for (int32 Kind = 0; Kind < 2; ++Kind)
	{
		const TCHAR* Name = Kind == 0 ? BiomassName : FleshName;
		UMaterial* Master = LoadQuietly<UMaterial>(Folder / Name, Name);
		if (!Master || bRebuild)
		{
			Master = BuildMaster(Folder, Kind == 1, Master, Detail, Options, OutError);
			if (!Master)
			{
				return false;
			}
		}
		Masters[Kind] = Master;
	}

	struct FSpec
	{
		const TCHAR* Name;
		UMaterialInterface* FEclipseMaterials::*Slot;
		int32 Master;
		TArray<TPair<FName, float>> Scalars;
	};
	const FSpec Specs[] = {
		{TEXT("MI_Eclipse_Overlay"), &FEclipseMaterials::Overlay, 0,
			{TPair<FName, float>(TEXT("OverlayMode"), 1.0f), TPair<FName, float>(TEXT("IsGrowth"), 0.0f)}},
		{TEXT("MI_Eclipse_Replace"), &FEclipseMaterials::Replace, 0,
			{TPair<FName, float>(TEXT("OverlayMode"), 0.0f), TPair<FName, float>(TEXT("IsGrowth"), 0.0f)}},
		{TEXT("MI_Eclipse_Growths"), &FEclipseMaterials::Growths, 0,
			{TPair<FName, float>(TEXT("OverlayMode"), 1.0f), TPair<FName, float>(TEXT("IsGrowth"), 1.0f)}},
		{TEXT("MI_Eclipse_Flesh"), &FEclipseMaterials::Flesh, 1, {}},
	};
	for (const FSpec& Spec : Specs)
	{
		UMaterialInstanceConstant* Existing = LoadQuietly<UMaterialInstanceConstant>(Folder / Spec.Name, Spec.Name);
		UMaterialInterface* Instance = Existing;
		if (!Existing || bRebuild)
		{
			Instance = BuildInstance(Folder, Spec.Name, Masters[Spec.Master], Existing, Spec.Scalars);
		}
		if (!Instance)
		{
			OutError = FText::Format(LOCTEXT("InstanceFailed", "Could not create {0}."), FText::FromString(Spec.Name));
			return false;
		}
		Out.*Spec.Slot = Instance;
	}

	// The halo must end where the grown shells do: keep the surfaces'
	// HaloReach in step with the setting the shells were grown with.
	for (UMaterialInterface* Surface : {Out.Overlay, Out.Replace})
	{
		auto* Instance = Cast<UMaterialInstanceConstant>(Surface);
		float Current = 0.0f;
		if (Instance && Instance->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("HaloReach")), Current) &&
			!FMath::IsNearlyEqual(Current, Options.HaloReach))
		{
			Instance->Modify();
			Instance->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(TEXT("HaloReach")), Options.HaloReach);
			Instance->PostEditChange();
			Instance->MarkPackageDirty();
		}
	}
	return true;
}

#undef LOCTEXT_NAMESPACE
