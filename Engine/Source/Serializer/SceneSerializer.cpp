#include "SceneSerializer.h"
#include "ThirdParty/nlohmann/json.hpp"
#include "Renderer/Resources/Material/Material.h"
#include "Renderer/Resources/Material/MaterialManager.h"
#include "Renderer/Renderer.h"
#include "Component/CameraComponent.h"
#include "Camera/Camera.h"
#include "Core/Paths.h"
#include "Core/Engine.h"
#include "Actor/Actor.h"
#include "Component/PrimitiveComponent.h"
#include "Level/Level.h"
#include "Object/ObjectFactory.h" 
#include "Serializer/Archive.h"
#include "Object/Class.h"
#include <algorithm>
#include <iomanip>
#include <filesystem>
#include <fstream>

namespace
{
	using json = nlohmann::json;

	bool TryReadFloat(const json& Value, float& OutValue)
	{
		try
		{
			if (Value.is_number())
			{
				OutValue = Value.get<float>();
				return true;
			}

			if (Value.is_array() && !Value.empty() && Value[0].is_number())
			{
				OutValue = Value[0].get<float>();
				return true;
			}
		}
		catch (const std::exception&)
		{
		}

		return false;
	}

	bool TryReadVector3(const json& Value, FVector& OutValue)
	{
		if (!Value.is_array() || Value.size() < 3)
		{
			return false;
		}

		try
		{
			OutValue = {
				Value[0].get<float>(),
				Value[1].get<float>(),
				Value[2].get<float>()
			};
			return true;
		}
		catch (const std::exception&)
		{
		}

		return false;
	}

	struct FLoadedActorEntry
	{
		AActor* Actor = nullptr;
		json ActorJson;
	};

	void FinalizeLoadedActors(const TArray<FLoadedActorEntry>& LoadedActors)
	{
		for (const FLoadedActorEntry& Entry : LoadedActors)
		{
			if (!Entry.Actor || Entry.Actor->IsPendingDestroy() || !Entry.ActorJson.is_object())
			{
				continue;
			}

			FArchive Ar(false);
			*static_cast<json*>(Ar.GetRawJson()) = Entry.ActorJson;
			Entry.Actor->Serialize(Ar);
		}
	}

	void LoadCameraDataFromJson(const json& RootJson, FCameraSerializeData* OutCameraData)
	{
		if (!OutCameraData || !RootJson.contains("PerspectiveCamera") || !RootJson["PerspectiveCamera"].is_object())
		{
			return;
		}

		const json& CameraJson = RootJson["PerspectiveCamera"];
		FVector Location = OutCameraData->Location;
		FVector RotationEuler(OutCameraData->Rotation.Pitch, OutCameraData->Rotation.Yaw, OutCameraData->Rotation.Roll);
		float FOV = OutCameraData->FOV;
		float NearClip = OutCameraData->NearClip;
		float FarClip = OutCameraData->FarClip;
		bool bReadAnyValue = false;

		if (CameraJson.contains("Location"))
		{
			bReadAnyValue |= TryReadVector3(CameraJson["Location"], Location);
		}
		if (CameraJson.contains("Rotation"))
		{
			bReadAnyValue |= TryReadVector3(CameraJson["Rotation"], RotationEuler);
		}
		if (CameraJson.contains("FOV"))
		{
			bReadAnyValue |= TryReadFloat(CameraJson["FOV"], FOV);
		}
		if (CameraJson.contains("NearClip"))
		{
			bReadAnyValue |= TryReadFloat(CameraJson["NearClip"], NearClip);
		}
		if (CameraJson.contains("FarClip"))
		{
			bReadAnyValue |= TryReadFloat(CameraJson["FarClip"], FarClip);
		}

		if (!bReadAnyValue)
		{
			return;
		}

		OutCameraData->Location = Location;
		OutCameraData->Rotation = FRotator(RotationEuler.X, RotationEuler.Y, RotationEuler.Z);
		OutCameraData->FOV = FOV;
		OutCameraData->NearClip = NearClip;
		OutCameraData->FarClip = FarClip;
		OutCameraData->bValid = true;
	}

	void PreloadMaterialsFromJson(const json& RootJson, ID3D11Device* Device)
	{
		if (!Device || !RootJson.contains("Materials") || !RootJson["Materials"].is_array())
		{
			return;
		}

		FRenderer* Renderer = GEngine ? GEngine->GetRenderer() : nullptr;
		if (!Renderer || !Renderer->GetRenderStateManager())
		{
			return;
		}

		for (const auto& MaterialValue : RootJson["Materials"])
		{
			if (!MaterialValue.is_string())
			{
				continue;
			}

			const FString RelativePath = MaterialValue.get<FString>();
			if (RelativePath.empty())
			{
				continue;
			}

			const FString AbsolutePath = FPaths::ToAbsolutePath(RelativePath);
			FMaterialManager::Get().LoadFromFile(Device, Renderer->GetRenderStateManager().get(), AbsolutePath);
		}
	}

	void SaveLevelSettingsToJson(const ULevel* Scene, json& RootJson)
	{
		if (!Scene)
		{
			return;
		}

		const FLevelGameplaySettings& Settings = Scene->GetGameplaySettings();
		json LevelSettingsJson;
		LevelSettingsJson["DefaultPawnMeshAsset"] = Settings.DefaultPawnMeshAsset;
		LevelSettingsJson["AutoSpawnPlayerStart"] = Settings.bAutoSpawnPlayerStart;
		LevelSettingsJson["DefaultPawnSpringArmLength"] = Settings.DefaultPawnSpringArmLength;
		LevelSettingsJson["DefaultPawnSpringArmSocketOffset"] =
		{
			Settings.DefaultPawnSpringArmSocketOffset.X,
			Settings.DefaultPawnSpringArmSocketOffset.Y,
			Settings.DefaultPawnSpringArmSocketOffset.Z
		};
		RootJson["LevelSettings"] = LevelSettingsJson;
	}

	void LoadLevelSettingsFromJson(const json& RootJson, ULevel* Scene)
	{
		if (!Scene || !RootJson.contains("LevelSettings") || !RootJson["LevelSettings"].is_object())
		{
			return;
		}

		const json& LevelSettingsJson = RootJson["LevelSettings"];
		FLevelGameplaySettings Settings = Scene->GetGameplaySettings();

		if (LevelSettingsJson.contains("DefaultPawnMeshAsset") && LevelSettingsJson["DefaultPawnMeshAsset"].is_string())
		{
			Settings.DefaultPawnMeshAsset = LevelSettingsJson["DefaultPawnMeshAsset"].get<FString>();
		}

		if (LevelSettingsJson.contains("AutoSpawnPlayerStart") && LevelSettingsJson["AutoSpawnPlayerStart"].is_boolean())
		{
			Settings.bAutoSpawnPlayerStart = LevelSettingsJson["AutoSpawnPlayerStart"].get<bool>();
		}

		if (LevelSettingsJson.contains("DefaultPawnSpringArmLength"))
		{
			float SpringArmLength = Settings.DefaultPawnSpringArmLength;
			if (TryReadFloat(LevelSettingsJson["DefaultPawnSpringArmLength"], SpringArmLength))
			{
				Settings.DefaultPawnSpringArmLength = std::max(0.0f, SpringArmLength);
			}
		}

		if (LevelSettingsJson.contains("DefaultPawnSpringArmSocketOffset"))
		{
			FVector SocketOffset = Settings.DefaultPawnSpringArmSocketOffset;
			if (TryReadVector3(LevelSettingsJson["DefaultPawnSpringArmSocketOffset"], SocketOffset))
			{
				Settings.DefaultPawnSpringArmSocketOffset = SocketOffset;
			}
		}

		Scene->SetGameplaySettings(Settings);
	}

	bool IsLegacyStaticMeshPrimitiveJson(const json& PrimitiveJson)
	{
		if (!PrimitiveJson.is_object())
		{
			return false;
		}

		if (PrimitiveJson.contains("Class") || PrimitiveJson.contains("Components"))
		{
			return false;
		}

		if (PrimitiveJson.contains("ObjStaticMeshAsset"))
		{
			return true;
		}

		if (!PrimitiveJson.contains("Type"))
		{
			return false;
		}

		try
		{
			return PrimitiveJson["Type"].get<FString>() == "StaticMeshComp";
		}
		catch (const std::exception&)
		{
			return false;
		}
	}

	json BuildModernActorJsonFromLegacyPrimitive(const json& LegacyPrimitiveJson)
	{
		json ComponentJson;
		ComponentJson["Class"] = "UStaticMeshComponent";

		if (LegacyPrimitiveJson.contains("Location"))
		{
			ComponentJson["Location"] = LegacyPrimitiveJson["Location"];
		}
		if (LegacyPrimitiveJson.contains("Rotation"))
		{
			ComponentJson["Rotation"] = LegacyPrimitiveJson["Rotation"];
		}
		if (LegacyPrimitiveJson.contains("Scale"))
		{
			ComponentJson["Scale"] = LegacyPrimitiveJson["Scale"];
		}
		if (LegacyPrimitiveJson.contains("ObjStaticMeshAsset"))
		{
			ComponentJson["ObjStaticMeshAsset"] = LegacyPrimitiveJson["ObjStaticMeshAsset"];
		}
		if (LegacyPrimitiveJson.contains("Materials"))
		{
			ComponentJson["Materials"] = LegacyPrimitiveJson["Materials"];
		}

		json ActorJson;
		ActorJson["Class"] = "AStaticMeshActor";
		ActorJson["Components"] = json::array({ ComponentJson });
		return ActorJson;
	}

	bool DeserializeActorFromJson(ULevel* Scene, const json& ActorJson, int32 ActorIndex, FLoadedActorEntry* OutLoadedActorEntry = nullptr)
	{
		if (!Scene || !ActorJson.is_object())
		{
			return false;
		}

		FString ClassName;
		try
		{
			ClassName = ActorJson.value("Class", "");
		}
		catch (const std::exception&)
		{
			return false;
		}

		if (ClassName.empty())
		{
			return false;
		}

		UClass* ActorClass = UClass::FindClass(ClassName);
		if (!ActorClass)
		{
			return false;
		}

		const FString ActorName = ClassName + "_" + std::to_string(ActorIndex);
		AActor* Actor = static_cast<AActor*>(FObjectFactory::ConstructObject(ActorClass, Scene, ActorName));
		if (!Actor)
		{
			return false;
		}

		Scene->RegisterActor(Actor);

		// PostSpawnInitialize를 먼저 호출해 기본 컴포넌트 구조를 갖춘 뒤,
		// 파일에서 복원할 때 UUID/이름 불일치로 중복 컴포넌트가 생기는 것을
		// 막기 위해 PostSpawnInitialize로 생성된 컴포넌트를 모두 제거한다.
		// Serialize 로드 경로가 파일 데이터를 기준으로 컴포넌트를 완전히 재구성한다.
		Actor->PostSpawnInitialize();

		{
			TArray<UActorComponent*> ToRemove(Actor->GetComponents().begin(), Actor->GetComponents().end());
			for (UActorComponent* Comp : ToRemove)
			{
				if (!Comp)
				{
					continue;
				}

				// GUUIDToObjectMap에서 즉시 제거한다.
				// MarkPendingKill만 하면 delete(CollectGarbage) 전까지 UUID가 맵에
				// 남아있어, 같은 씬의 다른 Actor 컴포넌트 복원 시 UUID 충돌이 발생한다.
				if (Comp->UUID != 0)
				{
					GUUIDToObjectMap.erase(Comp->UUID);
					Comp->UUID = 0;
				}

				Comp->MarkPendingKill();
				Actor->RemoveOwnedComponent(Comp);
			}
			Actor->SetRootComponent(nullptr);
		}

		try
		{
			FArchive Ar(false);
			*static_cast<json*>(Ar.GetRawJson()) = ActorJson;
			Actor->Serialize(Ar);
			if (OutLoadedActorEntry)
			{
				OutLoadedActorEntry->Actor = Actor;
				OutLoadedActorEntry->ActorJson = ActorJson;
			}
			return true;
		}
		catch (const std::exception&)
		{
			Scene->DestroyActor(Actor);
			return false;
		}
	}
}

void FSceneSerializer::Save(ULevel* Scene, const FString& FilePath, const FCameraSerializeData& CameraData)
{
	json Json;
	SaveLevelSettingsToJson(Scene, Json);
	if (CameraData.bValid)
	{
		Json["PerspectiveCamera"]["Location"]  = { CameraData.Location.X, CameraData.Location.Y, CameraData.Location.Z };
		Json["PerspectiveCamera"]["Rotation"]  = { CameraData.Rotation.Pitch, CameraData.Rotation.Yaw, CameraData.Rotation.Roll };
		Json["PerspectiveCamera"]["FOV"]       = CameraData.FOV;
		Json["PerspectiveCamera"]["NearClip"]  = CameraData.NearClip;
		Json["PerspectiveCamera"]["FarClip"]   = CameraData.FarClip;
	}

	// Materials (로드된 Material 파일 경로를 프로젝트 루트 기준 상대 경로로 저장)
	TArray<FString> LoadedPaths = FMaterialManager::Get().GetLoadedPaths();
	if (!LoadedPaths.empty())
	{
		nlohmann::json Materials = nlohmann::json::array();
		const std::filesystem::path Root = FPaths::ProjectRoot();
		for (const FString& AbsPath : LoadedPaths)
		{
			// 절대 경로 → 프로젝트 루트 기준 상대 경로
			std::error_code RelativeEc;
			std::filesystem::path Rel = std::filesystem::relative(FPaths::ToPath(AbsPath), Root, RelativeEc);
			if (RelativeEc || Rel.empty())
			{
				Rel = FPaths::ToPath(AbsPath);
			}

			FString RelativePath = FPaths::FromPath(Rel);
			std::replace(RelativePath.begin(), RelativePath.end(), '\\', '/');
			Materials.push_back(RelativePath);
		}
		Json["Materials"] = Materials;
	}

	// Primitives
	json Primitives;
	int32 Index = 0;
	for (AActor* Actor : Scene->GetActors())
	{
		if (!Actor || Actor->IsPendingDestroy())
			continue;
		if (!Actor->GetRootComponent())
			continue;
		FArchive Ar(true);
		Actor->Serialize(Ar);
		json& ActorJson 
			= *static_cast<nlohmann::json*>(Ar.GetRawJson());
		Primitives[std::to_string(Index)] = ActorJson;
		Index++;
	}

	Json["Primitives"] = Primitives;
	Json["NextUUID"] = FObjectFactory::GetLastUUID();
	std::ofstream File(FPaths::ToPath(FilePath));
	if (File.is_open())
	{
		File << std::setw(2) << Json << std::endl;
	}
}

bool FSceneSerializer::Load(ULevel* Scene, const FString& FilePath, ID3D11Device* Device,
                            FCameraSerializeData* OutCameraData)
{
	std::ifstream File(FPaths::ToPath(FilePath));
	if (!File.is_open())
	{
		return false;
	}

	json Json;

	try
	{
		File >> Json;
	}
	catch (const std::exception&)
	{
		return false;
	}

	if (!Json.contains("Primitives") || !Json["Primitives"].is_object())
		return false;

	LoadLevelSettingsFromJson(Json, Scene);

	if (OutCameraData)
	{
		LoadCameraDataFromJson(Json, OutCameraData);
	}

	PreloadMaterialsFromJson(Json, Device);

	bool bAllActorsLoaded = true;
	int32 ActorIndex = 0;
	TArray<FLoadedActorEntry> LoadedActors;
	LoadedActors.reserve(Json["Primitives"].size());
	for (auto& [Key, Value] : Json["Primitives"].items())
	{
		const json* ActorJson = &Value;
		json LegacyActorJson;
		if (IsLegacyStaticMeshPrimitiveJson(Value))
		{
			LegacyActorJson = BuildModernActorJsonFromLegacyPrimitive(Value);
			ActorJson = &LegacyActorJson;
		}

		FLoadedActorEntry& LoadedEntry = LoadedActors.emplace_back();
		bAllActorsLoaded &= DeserializeActorFromJson(Scene, *ActorJson, ActorIndex, &LoadedEntry);

		++ActorIndex;
	}

	FinalizeLoadedActors(LoadedActors);
	Scene->EnsureEssentialActors();
	Scene->EnsurePlayerStartActor();
	Scene->MarkSpatialDirty();

	if (Json.contains("NextUUID"))
	{
		uint32 Saved = Json["NextUUID"].get<uint32>();
		if (Saved > FObjectFactory::GetLastUUID())
		{
			FObjectFactory::SetLastUUID(Saved);
		}
	}

	return bAllActorsLoaded;
}
