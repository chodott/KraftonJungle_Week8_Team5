#include "Asset/ObjManager.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <vector>
#include <Windows.h>

#include "Core/Engine.h"
#include "Core/Paths.h"
#include "Debug/EngineLog.h"
#include "Math/MathUtility.h"
#include <map>

#include "StaticMeshLODBuilder.h"
#include "Object/ObjectFactory.h"
#include "Renderer/Resources/Material/Material.h"
#include "Renderer/Resources/Material/MaterialManager.h"
#include "Renderer/Renderer.h"
#include "Renderer/Resources/Shader/Shader.h"
#include "Renderer/Resources/Shader/ShaderMap.h"

TMap<FString, UStaticMesh*> FObjManager::ObjStaticMeshMap;

namespace
{
	constexpr char   GModelMagic[4]                 = { 'M', 'O', 'D', 'L' };
	constexpr uint32 GModelVersionLegacy            = 1;
	constexpr uint32 GModelVersionEmbeddedMaterials = 2;
	constexpr uint32 GModelVersionSourceTimestamp   = 3;
	constexpr uint32 GModelVersionNormalTexture		= 4;
	constexpr uint32 GModelVersionEmissiveTexture   = 5;
	constexpr uint32 GModelVersion                  = GModelVersionEmissiveTexture;

	constexpr char   GLODMagic[4]                    = { 'L', 'O', 'D', 'F' };
	constexpr uint32 GLODVersionLegacy               = 1;
	constexpr uint32 GLODVersionSourceTimestamp      = 2;
	constexpr uint32 GLODVersionScreenSize           = 3;
	constexpr uint32 GLODVersionDistance             = 4;
	constexpr uint32 GLODVersion                     = GLODVersionDistance;
	constexpr uint64 GWindowsToUnixEpoch100Ns        = 116444736000000000ULL;
	constexpr uint64 GFileTimeTickToNanoseconds      = 100ULL;
	constexpr uint64 GTimestampComparisonToleranceNs = 1000ULL;

	std::filesystem::path BuildSiblingPathWithExtension(const std::filesystem::path& BasePath, const FString& Suffix, const FString& Extension)
	{
		std::filesystem::path FileName = BasePath.stem();
		if (!Suffix.empty())
		{
			FileName += FPaths::ToPath(Suffix);
		}
		FileName += FPaths::ToPath(Extension);
		return BasePath.parent_path() / FileName;
	}

	FString GetLodFilePath(const FString& MeshPathFileName, int32 LodLevel)
	{
		const std::filesystem::path MeshPath = FPaths::ToPath(FPaths::ToAbsolutePath(MeshPathFileName)).lexically_normal();
		const std::filesystem::path LodPath  = BuildSiblingPathWithExtension(
			MeshPath,
			"_lod" + std::to_string(LodLevel),
			".lod");
		FString Result = FPaths::FromPath(LodPath);
		std::replace(Result.begin(), Result.end(), '\\', '/');
		return Result;
	}

	FString GetModelFilePath(const FString& MeshPathFileName)
	{
		const std::filesystem::path MeshPath = FPaths::ToPath(FPaths::ToAbsolutePath(MeshPathFileName)).lexically_normal();
		FString                     Result   = FPaths::FromPath(BuildSiblingPathWithExtension(MeshPath, "", ".model"));
		std::replace(Result.begin(), Result.end(), '\\', '/');
		return Result;
	}

	FString GetObjFilePathFromModelPath(const FString& ModelPathFileName)
	{
		const std::filesystem::path ModelPath = FPaths::ToPath(FPaths::ToAbsolutePath(ModelPathFileName)).lexically_normal();
		FString                     Result    = FPaths::FromPath(BuildSiblingPathWithExtension(ModelPath, "", ".obj"));
		std::replace(Result.begin(), Result.end(), '\\', '/');
		return Result;
	}

	FString GetNormalizedExtension(const FString& PathFileName);
	bool    ReadModelSourceTimestamp(const FString& ModelPathFileName, uint64& OutTimestamp);
	bool    ReadLodSourceTimestamp(const FString& LodPathFileName, uint64& OutTimestamp);
	void    RemoveFileIfExists(const std::filesystem::path& Path);
	void    RemoveModelArtifact(const FString& ObjPathFileName);
	void    RemoveLodArtifacts(const FString& ObjPathFileName);

	float GetDefaultLodDistance(const UStaticMesh& Asset, int32 LodLevel, float DistanceStep)
	{
		const float SafeBoundsRadius = (std::max)(Asset.LocalBounds.Radius, 1.0f);
		const float ClampedStep      = (std::max)(DistanceStep, 1.0f);
		return SafeBoundsRadius * ClampedStep * static_cast<float>(LodLevel);
	}

	float ConvertLegacyScreenSizeToDistance(const UStaticMesh& Asset, float ScreenSize, int32 LodLevel)
	{
		const float        SafeBoundsRadius       = (std::max)(Asset.LocalBounds.Radius, 1.0f);
		const float        SafeScreenSize         = (std::max)(ScreenSize, 0.0001f);
		constexpr float    LegacyProjectionScaleY = 1.7320508075688772f;
		const float        ApproxDistance         = SafeBoundsRadius * LegacyProjectionScaleY / SafeScreenSize;
		const FStaticMesh* ExistingLod            = Asset.GetRenderData(LodLevel);
		if (ExistingLod != nullptr)
		{
			const float AssetDistance = Asset.GetLodDistance(LodLevel);
			if (AssetDistance > 0.0f)
			{
				return AssetDistance;
			}
		}
		return ApproxDistance;
	}

	uint64 ConvertFileTimeToUnixNanoseconds(const FILETIME& FileTime)
	{
		ULARGE_INTEGER FileTimeValue = {};
		FileTimeValue.LowPart        = FileTime.dwLowDateTime;
		FileTimeValue.HighPart       = FileTime.dwHighDateTime;
		if (FileTimeValue.QuadPart <= GWindowsToUnixEpoch100Ns)
		{
			return 0;
		}

		return (FileTimeValue.QuadPart - GWindowsToUnixEpoch100Ns) * GFileTimeTickToNanoseconds;
	}

	bool AreSourceTimestampsEquivalent(uint64 StoredTimestamp, uint64 SourceTimestamp)
	{
		if (StoredTimestamp == SourceTimestamp)
		{
			return true;
		}

		if (StoredTimestamp == 0 || SourceTimestamp == 0)
		{
			return false;
		}

		const uint64 Delta = (StoredTimestamp > SourceTimestamp)
			                     ? (StoredTimestamp - SourceTimestamp)
			                     : (SourceTimestamp - StoredTimestamp);
		return Delta <= GTimestampComparisonToleranceNs;
	}

	uint64 GetFileWriteTimestamp(const std::filesystem::path& Path)
	{
		if (Path.empty())
		{
			return 0;
		}

		WIN32_FILE_ATTRIBUTE_DATA FileData = {};
		if (!GetFileAttributesExW(Path.c_str(), GetFileExInfoStandard, &FileData))
		{
			return 0;
		}

		return ConvertFileTimeToUnixNanoseconds(FileData.ftLastWriteTime);
	}

	uint64 GetMeshSourceTimestamp(const FString& MeshPathFileName)
	{
		const FString Extension = GetNormalizedExtension(MeshPathFileName);
		if (Extension == ".model")
		{
			const std::filesystem::path ObjPath = FPaths::ToPath(FPaths::ToAbsolutePath(GetObjFilePathFromModelPath(MeshPathFileName))).lexically_normal();
			std::error_code             ErrorCode;
			if (!ObjPath.empty() && std::filesystem::exists(ObjPath, ErrorCode) && !ErrorCode)
			{
				return GetFileWriteTimestamp(ObjPath);
			}

			uint64 CachedTimestamp = 0;
			if (ReadModelSourceTimestamp(MeshPathFileName, CachedTimestamp))
			{
				return CachedTimestamp;
			}
		}

		return GetFileWriteTimestamp(FPaths::ToPath(FPaths::ToAbsolutePath(MeshPathFileName)).lexically_normal());
	}

	void LoadAvailableLODs(UStaticMesh& Asset, const FString& PathFileName)
	{
		constexpr FStaticMeshLODSettings Settings;
		const uint64                     SourceTimestamp = GetMeshSourceTimestamp(PathFileName);
		Asset.ClearLods();

		for (int32 i = 1; i <= Settings.NumLODs; ++i)
		{
			const FString LodPath = GetLodFilePath(PathFileName, i);
			if (!std::filesystem::exists(FPaths::ToPath(LodPath)))
			{
				continue;
			}

			uint64 CachedTimestamp = 0;
			if (SourceTimestamp != 0)
			{
				const bool bReadOk = ReadLodSourceTimestamp(LodPath, CachedTimestamp);
				if (!bReadOk || !AreSourceTimestampsEquivalent(CachedTimestamp, SourceTimestamp))
				{
					continue;
				}
			}

			const float  DefaultLodDistance = GetDefaultLodDistance(Asset, i, Settings.DistanceStep);
			float        LoadedDistance     = DefaultLodDistance;
			FStaticMesh* LodMesh            = FObjManager::LoadLodAsset(LodPath, &LoadedDistance);
			if (LodMesh)
			{
				Asset.AddLod(std::unique_ptr<FStaticMesh>(LodMesh), LoadedDistance);
			}
		}
	}

	FString NormalizeSlashes(FString Path)
	{
		std::replace(Path.begin(), Path.end(), '\\', '/');
		return Path;
	}

	FString GetStandardizedMeshPath(const FString& InPath)
	{
		FString Path = NormalizeSlashes(InPath);
		if (Path.starts_with("Data/"))
		{
			Path = "Assets/Meshes/" + Path;
		}
		else if (Path.find('/') == std::string::npos)
		{
			Path = "Assets/Meshes/" + Path;
		}
		Path = FPaths::ToRelativePath(Path);
		Path = NormalizeSlashes(Path);

		const std::filesystem::path FsPath = FPaths::ToPath(Path);
		FString                     Ext    = FPaths::FromPath(FsPath.extension());
		std::transform(Ext.begin(),
		               Ext.end(),
		               Ext.begin(),
		               [](unsigned char c)
		               {
			               return static_cast<char>(std::tolower(c));
		               });
		Path = NormalizeSlashes(FPaths::FromPath(FsPath.parent_path() / FsPath.stem())) + Ext;

		return Path;
	}

	FString BuildObjCacheKey(const FString& PathFileName, const FObjLoadOptions& LoadOptions)
	{
		const FString StandardizedPath = GetStandardizedMeshPath(PathFileName);
		if (LoadOptions.bUseLegacyObjConversion)
		{
			return StandardizedPath + "|OBJ|LEGACY";
		}

		auto AxisToken = [](EObjImportAxis Axis) -> const char*
		{
			switch (Axis)
			{
			case EObjImportAxis::PosX: return "+X";
			case EObjImportAxis::NegX: return "-X";
			case EObjImportAxis::PosY: return "+Y";
			case EObjImportAxis::NegY: return "-Y";
			case EObjImportAxis::PosZ: return "+Z";
			case EObjImportAxis::NegZ: return "-Z";
			default: return "+X";
			}
		};

		return StandardizedPath + "|OBJ|F=" + AxisToken(LoadOptions.ForwardAxis) + "|U=" + AxisToken(LoadOptions.UpAxis);
	}

	int32 GetAxisBaseIndex(EObjImportAxis Axis)
	{
		switch (Axis)
		{
		case EObjImportAxis::PosX:
		case EObjImportAxis::NegX:
			return 0;
		case EObjImportAxis::PosY:
		case EObjImportAxis::NegY:
			return 1;
		case EObjImportAxis::PosZ:
		case EObjImportAxis::NegZ:
			return 2;
		default:
			return 0;
		}
	}

	float GetAxisSign(EObjImportAxis Axis)
	{
		switch (Axis)
		{
		case EObjImportAxis::NegX:
		case EObjImportAxis::NegY:
		case EObjImportAxis::NegZ:
			return -1.0f;
		default:
			return 1.0f;
		}
	}

	EObjImportAxis GetPositiveAxisByBaseIndex(int32 BaseIndex)
	{
		switch (BaseIndex)
		{
		case 0: return EObjImportAxis::PosX;
		case 1: return EObjImportAxis::PosY;
		case 2: return EObjImportAxis::PosZ;
		default: return EObjImportAxis::PosX;
		}
	}

	EObjImportAxis GetRemainingPositiveAxis(EObjImportAxis ForwardAxis, EObjImportAxis UpAxis)
	{
		const int32 ForwardBaseIndex = GetAxisBaseIndex(ForwardAxis);
		const int32 UpBaseIndex      = GetAxisBaseIndex(UpAxis);
		for (int32 BaseIndex = 0; BaseIndex < 3; ++BaseIndex)
		{
			if (BaseIndex != ForwardBaseIndex && BaseIndex != UpBaseIndex)
			{
				return GetPositiveAxisByBaseIndex(BaseIndex);
			}
		}

		return EObjImportAxis::PosY;
	}

	float GetVectorComponentForAxis(const FVector& Vector, EObjImportAxis Axis)
	{
		switch (Axis)
		{
		case EObjImportAxis::PosX: return Vector.X;
		case EObjImportAxis::NegX: return -Vector.X;
		case EObjImportAxis::PosY: return Vector.Y;
		case EObjImportAxis::NegY: return -Vector.Y;
		case EObjImportAxis::PosZ: return Vector.Z;
		case EObjImportAxis::NegZ: return -Vector.Z;
		default: return Vector.X;
		}
	}

	FVector ConvertObjVectorToEngineBasis(const FVector& Vector, const FObjLoadOptions& LoadOptions)
	{
		if (LoadOptions.bUseLegacyObjConversion)
		{
			FVector Converted = Vector;
			Converted.Y       = -Converted.Y;
			return Converted;
		}

		const EObjImportAxis RightAxis = GetRemainingPositiveAxis(LoadOptions.ForwardAxis, LoadOptions.UpAxis);
		return FVector(
			GetVectorComponentForAxis(Vector, LoadOptions.ForwardAxis),
			GetVectorComponentForAxis(Vector, RightAxis),
			GetVectorComponentForAxis(Vector, LoadOptions.UpAxis));
	}

	int32 GetObjConversionDeterminantSign(const FObjLoadOptions& LoadOptions)
	{
		if (LoadOptions.bUseLegacyObjConversion)
		{
			return -1;
		}

		const EObjImportAxis RightAxis                       = GetRemainingPositiveAxis(LoadOptions.ForwardAxis, LoadOptions.UpAxis);
		float                Matrix[3][3]                    = {};
		Matrix[0][GetAxisBaseIndex(LoadOptions.ForwardAxis)] = GetAxisSign(LoadOptions.ForwardAxis);
		Matrix[1][GetAxisBaseIndex(RightAxis)]               = GetAxisSign(RightAxis);
		Matrix[2][GetAxisBaseIndex(LoadOptions.UpAxis)]      = GetAxisSign(LoadOptions.UpAxis);

		const float Determinant =
				Matrix[0][0] * (Matrix[1][1] * Matrix[2][2] - Matrix[1][2] * Matrix[2][1]) -
				Matrix[0][1] * (Matrix[1][0] * Matrix[2][2] - Matrix[1][2] * Matrix[2][0]) +
				Matrix[0][2] * (Matrix[1][0] * Matrix[2][1] - Matrix[1][1] * Matrix[2][0]);
		return (Determinant < 0.0f) ? -1 : 1;
	}

	FString WideToUtf8(const std::wstring& WideString)
	{
		if (WideString.empty())
		{
			return "";
		}

		const int32 RequiredBytes = WideCharToMultiByte(
			CP_UTF8,
			0,
			WideString.c_str(),
			-1,
			nullptr,
			0,
			nullptr,
			nullptr);
		if (RequiredBytes <= 1)
		{
			return "";
		}

		FString Utf8String;
		Utf8String.resize(static_cast<size_t>(RequiredBytes));
		WideCharToMultiByte(
			CP_UTF8,
			0,
			WideString.c_str(),
			-1,
			Utf8String.data(),
			RequiredBytes,
			nullptr,
			nullptr);
		Utf8String.pop_back();
		return Utf8String;
	}

	FString PathToUtf8(const std::filesystem::path& Path)
	{
		return WideToUtf8(Path.wstring());
	}

	FString ObjFileStringToUtf8(const std::string& Str)
	{
		if (Str.empty())
		{
			return {};
		}
		if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, Str.c_str(), -1, nullptr, 0) > 0)
		{
			return Str;
		}
		const int WLen = MultiByteToWideChar(CP_ACP, 0, Str.c_str(), -1, nullptr, 0);
		if (WLen <= 1)
		{
			return {};
		}
		std::wstring Wide(WLen - 1, L'\0');
		MultiByteToWideChar(CP_ACP, 0, Str.c_str(), -1, Wide.data(), WLen);
		return WideToUtf8(Wide);
	}

	FString TrimAscii(const FString& Value)
	{
		size_t Start = 0;
		while (Start < Value.size() && std::isspace(static_cast<unsigned char>(Value[Start])))
		{
			++Start;
		}

		size_t End = Value.size();
		while (End > Start && std::isspace(static_cast<unsigned char>(Value[End - 1])))
		{
			--End;
		}

		return Value.substr(Start, End - Start);
	}

	bool PathExists(const std::filesystem::path& Path)
	{
		std::error_code ErrorCode;
		return !Path.empty() && std::filesystem::exists(Path, ErrorCode);
	}

	bool IsFileNewer(const std::filesystem::path& SourcePath, const std::filesystem::path& TargetPath)
	{
		std::error_code ErrorCode;
		if (!PathExists(SourcePath) || !PathExists(TargetPath))
		{
			return false;
		}

		const auto SourceWriteTime = std::filesystem::last_write_time(SourcePath, ErrorCode);
		if (ErrorCode)
		{
			return false;
		}

		const auto TargetWriteTime = std::filesystem::last_write_time(TargetPath, ErrorCode);
		if (ErrorCode)
		{
			return false;
		}

		return SourceWriteTime > TargetWriteTime;
	}

	void RemoveFileIfExists(const std::filesystem::path& Path)
	{
		std::error_code ErrorCode;
		if (PathExists(Path))
		{
			std::filesystem::remove(Path, ErrorCode);
		}
	}

	void RemoveModelArtifact(const FString& ObjPathFileName)
	{
		RemoveFileIfExists(FPaths::ToPath(GetModelFilePath(ObjPathFileName)));
	}

	void RemoveLodArtifacts(const FString& ObjPathFileName)
	{
		for (int32 LodLevel = 1; LodLevel <= 64; ++LodLevel)
		{
			RemoveFileIfExists(FPaths::ToPath(GetLodFilePath(ObjPathFileName, LodLevel)));
		}
	}

	void RemoveCachedArtifacts(const FString& ObjPathFileName)
	{
		RemoveModelArtifact(ObjPathFileName);
		RemoveLodArtifacts(ObjPathFileName);
	}

	bool ReadModelSourceTimestamp(const FString& ModelPathFileName, uint64& OutTimestamp)
	{
		OutTimestamp                         = 0;
		const std::filesystem::path FilePath = FPaths::ToPath(FPaths::ToAbsolutePath(ModelPathFileName)).lexically_normal();
		std::ifstream               File(FilePath, std::ios::binary);
		if (!File.is_open())
		{
			return false;
		}

		char   Magic[sizeof(GModelMagic)] = {};
		uint32 Version                    = 0;
		File.read(Magic, sizeof(Magic));
		File.read(reinterpret_cast<char*>(&Version), sizeof(Version));
		if (!File.good() || std::memcmp(Magic, GModelMagic, sizeof(GModelMagic)) != 0)
		{
			return false;
		}

		if (Version >= GModelVersionSourceTimestamp)
		{
			File.read(reinterpret_cast<char*>(&OutTimestamp), sizeof(OutTimestamp));
			return File.good();
		}

		return false;
	}

	bool ReadLodSourceTimestamp(const FString& LodPathFileName, uint64& OutTimestamp)
	{
		OutTimestamp                         = 0;
		const std::filesystem::path FilePath = FPaths::ToPath(FPaths::ToAbsolutePath(LodPathFileName)).lexically_normal();
		std::ifstream               File(FilePath, std::ios::binary);
		if (!File.is_open())
		{
			return false;
		}

		char   Magic[sizeof(GLODMagic)] = {};
		uint32 Version                  = 0;
		File.read(Magic, sizeof(Magic));
		File.read(reinterpret_cast<char*>(&Version), sizeof(Version));
		if (!File.good() || std::memcmp(Magic, GLODMagic, sizeof(GLODMagic)) != 0)
		{
			return false;
		}

		if (Version >= GLODVersionSourceTimestamp)
		{
			File.read(reinterpret_cast<char*>(&OutTimestamp), sizeof(OutTimestamp));
			return File.good();
		}

		return false;
	}

	template <typename T>
	bool WriteBinaryValue(std::ofstream& File, const T& Value)
	{
		File.write(reinterpret_cast<const char*>(&Value), sizeof(T));
		return File.good();
	}

	template <typename T>
	bool ReadBinaryValue(std::ifstream& File, T& Value)
	{
		File.read(reinterpret_cast<char*>(&Value), sizeof(T));
		return File.good();
	}

	bool WriteBinaryBytes(std::ofstream& File, const void* Data, std::streamsize Size)
	{
		if (Size <= 0)
		{
			return true;
		}

		File.write(reinterpret_cast<const char*>(Data), Size);
		return File.good();
	}

	bool ReadBinaryBytes(std::ifstream& File, void* Data, std::streamsize Size)
	{
		if (Size <= 0)
		{
			return true;
		}

		File.read(reinterpret_cast<char*>(Data), Size);
		return File.good();
	}

	bool WriteUtf8String(std::ofstream& File, const FString& Value)
	{
		const uint32 ByteCount = static_cast<uint32>(Value.size());
		if (!WriteBinaryValue(File, ByteCount))
		{
			return false;
		}

		return WriteBinaryBytes(File, Value.data(), ByteCount);
	}

	bool ReadUtf8String(std::ifstream& File, FString& OutValue)
	{
		uint32 ByteCount = 0;
		if (!ReadBinaryValue(File, ByteCount))
		{
			return false;
		}

		OutValue.resize(ByteCount);
		return ReadBinaryBytes(File, OutValue.data(), ByteCount);
	}

	bool IsValidObjImportAxisValue(uint8 AxisValue)
	{
		return AxisValue <= static_cast<uint8>(EObjImportAxis::NegZ);
	}

	bool WriteModelImportOptionsPayload(std::ofstream& File, const FObjLoadOptions* LoadOptions)
	{
		const uint8 bHasLoadOptions = (LoadOptions != nullptr) ? 1 : 0;
		if (!WriteBinaryValue(File, bHasLoadOptions))
		{
			return false;
		}

		if (!bHasLoadOptions)
		{
			return true;
		}

		const uint8 bUseLegacyObjConversion = LoadOptions->bUseLegacyObjConversion ? 1 : 0;
		const uint8 ForwardAxis             = static_cast<uint8>(LoadOptions->ForwardAxis);
		const uint8 UpAxis                  = static_cast<uint8>(LoadOptions->UpAxis);
		return WriteBinaryValue(File, bUseLegacyObjConversion)
			&& WriteBinaryValue(File, ForwardAxis)
			&& WriteBinaryValue(File, UpAxis);
	}

	bool ReadModelImportOptionsPayload(std::ifstream& File, FObjLoadOptions& OutLoadOptions, bool& bOutHasLoadOptions)
	{
		uint8 bHasLoadOptions = 0;
		if (!ReadBinaryValue(File, bHasLoadOptions))
		{
			return false;
		}

		bOutHasLoadOptions = (bHasLoadOptions != 0);
		OutLoadOptions     = FObjLoadOptions {};
		if (!bOutHasLoadOptions)
		{
			return true;
		}

		uint8 bUseLegacyObjConversion = 0;
		uint8 ForwardAxis             = 0;
		uint8 UpAxis                  = 0;
		if (!ReadBinaryValue(File, bUseLegacyObjConversion)
			|| !ReadBinaryValue(File, ForwardAxis)
			|| !ReadBinaryValue(File, UpAxis))
		{
			return false;
		}

		if (!IsValidObjImportAxisValue(ForwardAxis) || !IsValidObjImportAxisValue(UpAxis))
		{
			return false;
		}

		OutLoadOptions.bUseLegacyObjConversion = (bUseLegacyObjConversion != 0);
		OutLoadOptions.ForwardAxis             = static_cast<EObjImportAxis>(ForwardAxis);
		OutLoadOptions.UpAxis                  = static_cast<EObjImportAxis>(UpAxis);
		return true;
	}

	std::filesystem::path ResolveMaterialReferencePath(const std::filesystem::path& ObjPath, const FString& MaterialReference)
	{
		const std::filesystem::path ReferencePath = std::filesystem::path(FPaths::ToWide(MaterialReference)).lexically_normal();
		if (ReferencePath.is_absolute() && PathExists(ReferencePath))
		{
			return ReferencePath;
		}

		const TArray<std::filesystem::path> Candidates =
		{
			(ObjPath.parent_path() / ReferencePath).lexically_normal(),
			(FPaths::MaterialDir() / ReferencePath).lexically_normal(),
			(FPaths::MaterialDir() / ReferencePath.filename()).lexically_normal(),
			(FPaths::ProjectRoot() / ReferencePath).lexically_normal()
		};

		for (const std::filesystem::path& Candidate : Candidates)
		{
			if (PathExists(Candidate))
			{
				return Candidate;
			}
		}

		return (ObjPath.parent_path() / ReferencePath).lexically_normal();
	}

	std::filesystem::path ResolveTextureReferencePath(const std::filesystem::path& SourceFilePath, const FString& TextureReference)
	{
		const FString TrimmedReference = TrimAscii(TextureReference);
		if (TrimmedReference.empty())
		{
			return {};
		}

		const std::filesystem::path ReferencePath = std::filesystem::path(FPaths::ToWide(TrimmedReference)).lexically_normal();
		if (ReferencePath.is_absolute() && PathExists(ReferencePath))
		{
			return ReferencePath;
		}

		const TArray<std::filesystem::path> Candidates =
		{
			(SourceFilePath.parent_path() / ReferencePath).lexically_normal(),
			(FPaths::ProjectRoot() / ReferencePath).lexically_normal(),
			(FPaths::TextureDir() / ReferencePath).lexically_normal(),
			(FPaths::TextureDir() / ReferencePath.filename()).lexically_normal()
		};

		for (const std::filesystem::path& Candidate : Candidates)
		{
			if (PathExists(Candidate))
			{
				return Candidate;
			}
		}

		return (SourceFilePath.parent_path() / ReferencePath).lexically_normal();
	}

	FString MakeStoredTexturePath(const std::filesystem::path& ModelFilePath, const std::filesystem::path& TexturePath)
	{
		if (TexturePath.empty())
		{
			return "";
		}

		const std::filesystem::path BaseDirectory = ModelFilePath.parent_path().empty()
			                                            ? FPaths::ProjectRoot()
			                                            : ModelFilePath.parent_path();
		const std::filesystem::path RelativePath = TexturePath.lexically_relative(BaseDirectory);
		if (!RelativePath.empty())
		{
			return PathToUtf8(RelativePath);
		}

		return PathToUtf8(TexturePath);
	}

	FString GetNormalizedExtension(const FString& PathFileName)
	{
		FString Extension = FPaths::FromPath(FPaths::ToPath(PathFileName).extension());
		std::transform(Extension.begin(),
		               Extension.end(),
		               Extension.begin(),
		               [](unsigned char Ch)
		               {
			               return static_cast<char>(std::tolower(Ch));
		               });
		return Extension;
	}

	uint32 GetRequiredMaterialSlotCount(const FStaticMesh& StaticMesh, const TArray<FString>& MaterialSlotNames)
	{
		uint32 SlotCount = static_cast<uint32>(MaterialSlotNames.size());
		for (const FMeshSection& Section : StaticMesh.Sections)
		{
			SlotCount = (std::max)(SlotCount, Section.MaterialIndex + 1);
		}
		return SlotCount;
	}

	uint32 GetRequiredMaterialSlotCount(const FStaticMesh& StaticMesh, const TArray<FModelMaterialInfo>& MaterialInfos)
	{
		uint32 SlotCount = static_cast<uint32>(MaterialInfos.size());
		for (const FMeshSection& Section : StaticMesh.Sections)
		{
			SlotCount = (std::max)(SlotCount, Section.MaterialIndex + 1);
		}
		return SlotCount;
	}

	FString GetMaterialSlotNameOrDefault(const TArray<FString>& MaterialSlotNames, uint32 SlotIndex)
	{
		if (SlotIndex < MaterialSlotNames.size() && !MaterialSlotNames[SlotIndex].empty())
		{
			return MaterialSlotNames[SlotIndex];
		}

		return "M_Default";
	}

	FModelMaterialInfo GetMaterialInfoOrDefault(const TArray<FModelMaterialInfo>& MaterialInfos, uint32 SlotIndex)
	{
		if (SlotIndex < MaterialInfos.size())
		{
			FModelMaterialInfo MaterialInfo = MaterialInfos[SlotIndex];
			if (MaterialInfo.Name.empty())
			{
				MaterialInfo.Name = "M_Default";
			}
			return MaterialInfo;
		}

		return {};
	}

	TArray<FString> BuildMaterialSlotNames(const UStaticMesh* Mesh)
	{
		TArray<FString> MaterialSlotNames;
		if (Mesh == nullptr || Mesh->GetRenderData() == nullptr)
		{
			return MaterialSlotNames;
		}

		uint32 SlotCount = static_cast<uint32>(Mesh->GetDefaultMaterials().size());
		for (const FMeshSection& Section : Mesh->GetRenderData()->Sections)
		{
			SlotCount = (std::max)(SlotCount, Section.MaterialIndex + 1);
		}

		if (SlotCount == 0)
		{
			SlotCount = 1;
		}

		MaterialSlotNames.resize(SlotCount, "M_Default");
		const TArray<std::shared_ptr<FMaterial>>& DefaultMaterials = Mesh->GetDefaultMaterials();
		for (uint32 SlotIndex = 0; SlotIndex < SlotCount && SlotIndex < DefaultMaterials.size(); ++SlotIndex)
		{
			const std::shared_ptr<FMaterial>& Material = DefaultMaterials[SlotIndex];
			if (Material && !Material->GetOriginName().empty())
			{
				MaterialSlotNames[SlotIndex] = Material->GetOriginName();
			}
		}

		return MaterialSlotNames;
	}

	std::shared_ptr<FMaterial> CreateImportedMaterialTemplate(const FString& MaterialName)
	{
		auto Material = std::make_shared<FMaterial>();
		Material->SetOriginName(MaterialName.empty() ? "M_Default" : MaterialName);

		std::wstring VSPath = FPaths::ShaderDir() / L"SceneGeometry/VertexShader.hlsl";
		std::wstring PSPath = FPaths::ShaderDir() / L"SceneGeometry/ColorPixelShader.hlsl";
		//std::wstring VSPath = FPaths::ShaderDir() / L"SceneLighting/UberLitVertexShader.hlsl";
		//std::wstring PSPath = FPaths::ShaderDir() / L"SceneLighting/UberLitPixelShader.hlsl";
		Material->SetVertexShader(FShaderMap::Get().GetOrCreateVertexShader(GEngine->GetRenderer()->GetDevice(), VSPath.c_str()));
		Material->SetPixelShader(FShaderMap::Get().GetOrCreatePixelShader(GEngine->GetRenderer()->GetDevice(), PSPath.c_str()));

		FMaterial* DefaultTexMat = GEngine->GetRenderer()->GetDefaultTextureMaterial();
		Material->SetRasterizerOption(DefaultTexMat->GetRasterizerOption());
		Material->SetRasterizerState(DefaultTexMat->GetRasterizerState());
		Material->SetDepthStencilOption(DefaultTexMat->GetDepthStencilOption());
		Material->SetDepthStencilState(DefaultTexMat->GetDepthStencilState());
		Material->SetBlendOption(DefaultTexMat->GetBlendOption());
		Material->SetBlendState(DefaultTexMat->GetBlendState());

		int32 SlotIndex = Material->CreateConstantBuffer(GEngine->GetRenderer()->GetDevice(), 64);
		if (SlotIndex >= 0)
		{
			Material->RegisterParameter("BaseColor", SlotIndex, 0, 16);
			constexpr float White[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
			Material->GetConstantBuffer(SlotIndex)->SetData(White, sizeof(White));

			Material->RegisterParameter("UVScrollSpeed", SlotIndex, 16, 16);
			constexpr float DefaultScroll[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
			Material->GetConstantBuffer(SlotIndex)->SetData(DefaultScroll, sizeof(DefaultScroll), 16);

			Material->RegisterParameter("EmissiveColor", SlotIndex, 32, 16);
			constexpr float DefaultEmissive[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
			Material->GetConstantBuffer(SlotIndex)->SetData(DefaultEmissive, sizeof(DefaultEmissive), 32);

			Material->RegisterParameter("Shininess", SlotIndex, 48, 16);
			constexpr float DefaultShininess[4] = { 32.0f, 0.0f, 0.0f, 0.0f };
			Material->GetConstantBuffer(SlotIndex)->SetData(DefaultShininess, sizeof(DefaultShininess), 48);
		}

		GEngine->GetRenderer()->ConfigureMaterialPasses(*Material, false);
		return Material;
	}

	void ApplyBaseColorToMaterial(const std::shared_ptr<FMaterial>& Material, const FLinearColor& BaseColor)
	{
		if (!Material)
		{
			return;
		}

		Material->SetLinearColorParameter("BaseColor", BaseColor);
	}

	bool TryLoadTextureIntoMaterial(const std::shared_ptr<FMaterial>& Material, const std::filesystem::path& TexturePath, const char* LogPrefix)
	{
		if (!Material || TexturePath.empty())
		{
			return false;
		}

		ID3D11ShaderResourceView* NewSRV = nullptr;
		if (!GEngine->GetRenderer()->CreateTextureFromSTB(
			GEngine->GetRenderer()->GetDevice(),
			TexturePath,
			&NewSRV,
			ETextureColorSpace::ColorSRGB))
		{
			return false;
		}

		auto MaterialTexture        = std::make_shared<FMaterialTexture>();
		MaterialTexture->TextureSRV = NewSRV;
		Material->SetMaterialTexture(MaterialTexture);

		//std::wstring TexPSPath = FPaths::ShaderDir() / L"SceneGeometry/TexturePixelShader.hlsl";
		std::wstring TexPSPath = FPaths::ShaderDir() / L"SceneLighting/UberLitPixelShader.hlsl";
		Material->SetPixelShader(FShaderMap::Get().GetOrCreatePixelShader(GEngine->GetRenderer()->GetDevice(), TexPSPath.c_str()));

		//std::wstring TexVSPath = FPaths::ShaderDir() / L"SceneGeometry/TextureVertexShader.hlsl";
		std::wstring TexVSPath = FPaths::ShaderDir() / L"SceneLighting/UberLitVertexShader.hlsl";
		Material->SetVertexShader(FShaderMap::Get().GetOrCreateVertexShader(GEngine->GetRenderer()->GetDevice(), TexVSPath.c_str()));
		GEngine->GetRenderer()->ConfigureMaterialPasses(*Material, true);
		UE_LOG("%s %s", LogPrefix, WideToUtf8(TexturePath.wstring()).c_str());
		return true;
	}

	FString ExtractTextureReferenceFromMtlStatement(const std::string& RawValue)
	{
		const FString Trimmed = ObjFileStringToUtf8(TrimAscii(RawValue));
		if (Trimmed.empty())
		{
			return "";
		}

		std::stringstream SS(Trimmed);
		TArray<FString> Tokens;
		FString Token;
		while (SS >> Token)
		{
			Tokens.push_back(Token);
		}

		return Tokens.empty() ? "" : Tokens.back();
	}

	bool TryLoadNormalTextureIntoMaterial(const std::shared_ptr<FMaterial>& Material, const std::filesystem::path& TexturePath, const char* LogPrefix)
	{
		if (!Material || TexturePath.empty())
		{
			return false;
		}

		ID3D11ShaderResourceView* NewSRV = nullptr;
		if (!GEngine->GetRenderer()->CreateTextureFromSTB(
			GEngine->GetRenderer()->GetDevice(),
			TexturePath,
			&NewSRV,
			ETextureColorSpace::DataLinear))
		{
			return false;
		}

		auto NormalTexture = std::make_shared<FMaterialTexture>();
		NormalTexture->TextureSRV = NewSRV;
		Material->SetNormalTexture(NormalTexture);
		UE_LOG("%s %s", LogPrefix, WideToUtf8(TexturePath.wstring()).c_str());
		return true;
	}

	bool TryLoadEmissiveTextureIntoMaterial(const std::shared_ptr<FMaterial>& Material, const std::filesystem::path& TexturePath, const char* LogPrefix)
	{
		if (!Material || TexturePath.empty())
		{
			return false;
		}

		ID3D11ShaderResourceView* NewSRV = nullptr;
		if (!GEngine->GetRenderer()->CreateTextureFromSTB(
			GEngine->GetRenderer()->GetDevice(),
			TexturePath,
			&NewSRV,
			ETextureColorSpace::ColorSRGB))
		{
			return false;
		}

		auto EmissiveTexture = std::make_shared<FMaterialTexture>();
		EmissiveTexture->TextureSRV = NewSRV;
		Material->SetEmissiveTexture(EmissiveTexture);
		UE_LOG("%s %s", LogPrefix, WideToUtf8(TexturePath.wstring()).c_str());
		return true;
	}

	struct FQuantizedPositionKey
	{
		int32 X = 0;
		int32 Y = 0;
		int32 Z = 0;

		bool operator==(const FQuantizedPositionKey& Other) const noexcept
		{
			return X == Other.X && Y == Other.Y && Z == Other.Z;
		}
	};

	struct FQuantizedPositionKeyHasher
	{
		size_t operator()(const FQuantizedPositionKey& Key) const noexcept
		{
			size_t Seed = static_cast<size_t>(Key.X);
			Seed ^= static_cast<size_t>(Key.Y) + 0x9e3779b9 + (Seed << 6) + (Seed >> 2);
			Seed ^= static_cast<size_t>(Key.Z) + 0x9e3779b9 + (Seed << 6) + (Seed >> 2);
			return Seed;
		}
	};

	FQuantizedPositionKey MakeQuantizedPositionKey(const FVector& Position, float QuantizationStep)
	{
		const float SafeStep = QuantizationStep > 0.0f ? QuantizationStep : 0.0001f;
		return FQuantizedPositionKey {
			static_cast<int32>(std::lround(Position.X / SafeStep)),
			static_cast<int32>(std::lround(Position.Y / SafeStep)),
			static_cast<int32>(std::lround(Position.Z / SafeStep))
		};
	}

	bool ShouldAutoSmoothNormals(const TArray<FString>& MaterialSlotNames)
	{
		if (MaterialSlotNames.empty())
		{
			return true;
		}

		for (const FString& SlotName : MaterialSlotNames)
		{
			const FString MaterialName = SlotName.empty() ? "M_Default" : SlotName;
			std::shared_ptr<FMaterial> Material = FMaterialManager::Get().FindByName(MaterialName);
			if (!Material && MaterialName != "M_Default")
			{
				Material = FMaterialManager::Get().FindByName("M_Default");
			}

			if (Material && Material->HasNormalTexture())
			{
				return false;
			}
		}

		return true;
	}

	bool ShouldAutoSmoothNormals(const TArray<FModelMaterialInfo>& MaterialInfos)
	{
		if (MaterialInfos.empty())
		{
			return true;
		}

		for (const FModelMaterialInfo& MaterialInfo : MaterialInfos)
		{
			if (!MaterialInfo.NormalTexturePath.empty())
			{
				return false;
			}
		}

		return true;
	}

	void CalculateSmoothNormals(FStaticMesh& Mesh)
	{
		if (Mesh.Vertices.empty() || Mesh.Indices.size() < 3)
		{
			return;
		}

		constexpr float PositionQuantizationStep = 0.0001f;
		constexpr float SmoothingAngleDegrees = 88.0f;
		const float CosSmoothingAngle = std::cos(FMath::DegreesToRadians(SmoothingAngleDegrees));

		const size_t TriangleCount = Mesh.Indices.size() / 3;
		std::vector<FVector> FaceWeightedNormals(TriangleCount, FVector::ZeroVector);
		std::vector<FVector> FaceUnitNormals(TriangleCount, FVector::ZeroVector);
		std::vector<std::vector<uint32>> VertexToTriangles(Mesh.Vertices.size());
		std::unordered_map<FQuantizedPositionKey, std::vector<uint32>, FQuantizedPositionKeyHasher> PositionToVertices;
		PositionToVertices.reserve(Mesh.Vertices.size());

		for (uint32 VertexIndex = 0; VertexIndex < Mesh.Vertices.size(); ++VertexIndex)
		{
			PositionToVertices[MakeQuantizedPositionKey(Mesh.Vertices[VertexIndex].Position, PositionQuantizationStep)].push_back(VertexIndex);
		}

		for (size_t TriangleIndex = 0; TriangleIndex < TriangleCount; ++TriangleIndex)
		{
			const size_t IndexBase = TriangleIndex * 3;
			const uint32 I0 = Mesh.Indices[IndexBase + 0];
			const uint32 I1 = Mesh.Indices[IndexBase + 1];
			const uint32 I2 = Mesh.Indices[IndexBase + 2];
			if (I0 >= Mesh.Vertices.size() || I1 >= Mesh.Vertices.size() || I2 >= Mesh.Vertices.size())
			{
				continue;
			}

			const FVector WeightedNormal = FVector::CrossProduct(
				Mesh.Vertices[I1].Position - Mesh.Vertices[I0].Position,
				Mesh.Vertices[I2].Position - Mesh.Vertices[I0].Position);
			const FVector UnitNormal = WeightedNormal.GetSafeNormal();
			if (UnitNormal.IsZero())
			{
				continue;
			}

			FaceWeightedNormals[TriangleIndex] = WeightedNormal;
			FaceUnitNormals[TriangleIndex] = UnitNormal;

			VertexToTriangles[I0].push_back(static_cast<uint32>(TriangleIndex));
			VertexToTriangles[I1].push_back(static_cast<uint32>(TriangleIndex));
			VertexToTriangles[I2].push_back(static_cast<uint32>(TriangleIndex));
		}

		for (uint32 VertexIndex = 0; VertexIndex < Mesh.Vertices.size(); ++VertexIndex)
		{
			const std::vector<uint32>& DirectTriangles = VertexToTriangles[VertexIndex];
			if (DirectTriangles.empty())
			{
				continue;
			}

			std::vector<uint32> CandidateTriangles = DirectTriangles;
			const auto PositionIt = PositionToVertices.find(
				MakeQuantizedPositionKey(Mesh.Vertices[VertexIndex].Position, PositionQuantizationStep));
			if (PositionIt != PositionToVertices.end())
			{
				for (uint32 SharedVertexIndex : PositionIt->second)
				{
					if (SharedVertexIndex == VertexIndex || SharedVertexIndex >= VertexToTriangles.size())
					{
						continue;
					}

					const std::vector<uint32>& SharedTriangles = VertexToTriangles[SharedVertexIndex];
					CandidateTriangles.insert(CandidateTriangles.end(), SharedTriangles.begin(), SharedTriangles.end());
				}
			}

			std::sort(CandidateTriangles.begin(), CandidateTriangles.end());
			CandidateTriangles.erase(std::unique(CandidateTriangles.begin(), CandidateTriangles.end()), CandidateTriangles.end());

			FVector ReferenceNormal = FVector::ZeroVector;
			for (uint32 TriangleIndex : DirectTriangles)
			{
				ReferenceNormal += FaceWeightedNormals[TriangleIndex];
			}
			ReferenceNormal = ReferenceNormal.GetSafeNormal();

			if (ReferenceNormal.IsZero())
			{
				for (uint32 TriangleIndex : CandidateTriangles)
				{
					ReferenceNormal += FaceWeightedNormals[TriangleIndex];
				}
				ReferenceNormal = ReferenceNormal.GetSafeNormal();
			}

			if (ReferenceNormal.IsZero())
			{
				continue;
			}

			FVector SmoothedNormal = FVector::ZeroVector;
			for (uint32 TriangleIndex : CandidateTriangles)
			{
				if (TriangleIndex >= FaceUnitNormals.size())
				{
					continue;
				}

				const FVector& FaceNormal = FaceUnitNormals[TriangleIndex];
				if (FaceNormal.IsZero())
				{
					continue;
				}

				if (FVector::DotProduct(ReferenceNormal, FaceNormal) >= CosSmoothingAngle)
				{
					SmoothedNormal += FaceWeightedNormals[TriangleIndex];
				}
			}

			const FVector FinalNormal = SmoothedNormal.GetSafeNormal();
			if (!FinalNormal.IsZero())
			{
				Mesh.Vertices[VertexIndex].Normal = FinalNormal;
			}
		}
	}

	void CalculateTangents(FStaticMesh& Mesh)
	{
		std::vector<FVector> TangentAccum(Mesh.Vertices.size(), FVector::Zero());
		std::vector<FVector> BitangentAccum(Mesh.Vertices.size(), FVector::Zero());

		for (FVertex& Vertex : Mesh.Vertices)
		{
			Vertex.Tangent = FVector4(0.f, 0.f, 0.f, 1.f);
		}

		for (size_t i = 0; i < Mesh.Indices.size(); i += 3)
		{
			const FVertex& V0 = Mesh.Vertices[Mesh.Indices[i + 0]];
			const FVertex& V1 = Mesh.Vertices[Mesh.Indices[i + 1]];
			const FVertex& V2 = Mesh.Vertices[Mesh.Indices[i + 2]];

			FVector E1 = V1.Position - V0.Position;
			FVector E2 = V2.Position - V0.Position;

			float dU1 = V1.UV.X - V0.UV.X;
			float dV1 = V1.UV.Y - V0.UV.Y;
			float dU2 = V2.UV.X - V0.UV.X;
			float dV2 = V2.UV.Y - V0.UV.Y;

			float det = dU1 * dV2 - dU2 * dV1;
			if (abs(det) < 1e-6f)
			{
				continue;
			}

			float invDet = 1.0f / det;

			FVector Tangent;
			Tangent.X = invDet * (dV2 * E1.X - dV1 * E2.X);
			Tangent.Y = invDet * (dV2 * E1.Y - dV1 * E2.Y);
			Tangent.Z = invDet * (dV2 * E1.Z - dV1 * E2.Z);

			FVector Bitangent;
			Bitangent.X = invDet * (-dU2 * E1.X + dU1 * E2.X);
			Bitangent.Y = invDet * (-dU2 * E1.Y + dU1 * E2.Y);
			Bitangent.Z = invDet * (-dU2 * E1.Z + dU1 * E2.Z);

			const uint32 Index0 = Mesh.Indices[i + 0];
			const uint32 Index1 = Mesh.Indices[i + 1];
			const uint32 Index2 = Mesh.Indices[i + 2];

			TangentAccum[Index0] += Tangent;
			TangentAccum[Index1] += Tangent;
			TangentAccum[Index2] += Tangent;

			BitangentAccum[Index0] += Bitangent;
			BitangentAccum[Index1] += Bitangent;
			BitangentAccum[Index2] += Bitangent;
		}

		for (size_t VertexIndex = 0; VertexIndex < Mesh.Vertices.size(); ++VertexIndex)
		{
			FVertex& Vertex = Mesh.Vertices[VertexIndex];
			FVector N = Vertex.Normal;
			FVector T = TangentAccum[VertexIndex];
			FVector B = BitangentAccum[VertexIndex];

			T -= N * FVector::DotProduct(T, N);

			if (T.Normalize())
			{
				const FVector OrthoBitangent = FVector::CrossProduct(N, T);
				const float Handedness = FVector::DotProduct(OrthoBitangent, B) < 0.0f ? -1.0f : 1.0f;
				Vertex.Tangent = FVector4(T.X, T.Y, T.Z, Handedness);
			}
			else
			{
				Vertex.Tangent = FVector4(1.f, 0.f, 0.f, 1.f);
			}
		}
	}

	UStaticMesh* FinalizeStaticMeshAsset(
		const FString&               PathFileName,
		std::unique_ptr<FStaticMesh> RawData,
		const TArray<FString>&       MaterialSlotNames)
	{
		FString JustFileName = FPaths::FromPath(FPaths::ToPath(PathFileName).filename());

		RawData->PathFileName = JustFileName;
		RawData->UpdateLocalBound();

		const FVector MeshCenter = RawData->GetCenterCoord();
		for (FVertex& Vertex : RawData->Vertices)
		{
			Vertex.Position.X -= MeshCenter.X;
			Vertex.Position.Y -= MeshCenter.Y;
			Vertex.Position.Z -= MeshCenter.Z;
		}
		RawData->UpdateLocalBound(); // 재계산

		if (ShouldAutoSmoothNormals(MaterialSlotNames))
		{
			CalculateSmoothNormals(*RawData);
		}

		CalculateTangents(*RawData);

		UStaticMesh* NewAsset = FObjectFactory::ConstructObject<UStaticMesh>(nullptr, JustFileName);
		NewAsset->SetStaticMeshAsset(RawData.release());

		NewAsset->LocalBounds.Radius    = NewAsset->GetRenderData()->GetLocalBoundRadius();
		NewAsset->LocalBounds.Center    = NewAsset->GetRenderData()->GetCenterCoord();
		NewAsset->LocalBounds.BoxExtent = (NewAsset->GetRenderData()->GetMaxCoord() - NewAsset->GetRenderData()->GetMinCoord()) * 0.5f;

		uint32 SlotCount = GetRequiredMaterialSlotCount(*NewAsset->GetRenderData(), MaterialSlotNames);
		if (SlotCount == 0)
		{
			SlotCount = 1;
		}

		for (uint32 SlotIndex = 0; SlotIndex < SlotCount; ++SlotIndex)
		{
			const FString              MaterialName = GetMaterialSlotNameOrDefault(MaterialSlotNames, SlotIndex);
			std::shared_ptr<FMaterial> Material     = FMaterialManager::Get().FindByName(MaterialName);
			if (!Material)
			{
				UE_LOG("[Warning] Static mesh requested missing material '%s'. Falling back to M_Default.", MaterialName.c_str());
				Material = FMaterialManager::Get().FindByName("M_Default");
			}

			NewAsset->AddDefaultMaterial(Material);
		}
		NewAsset->BuildAccelerationStructureIfNeeded();

		LoadAvailableLODs(*NewAsset, PathFileName);
		return NewAsset;
	}

	bool BuildModelCacheForObj(const FString& ObjPathFileName, const FObjLoadOptions& LoadOptions)
	{
		UStaticMesh* LoadedMesh = FObjManager::LoadObjStaticMeshAsset(ObjPathFileName, LoadOptions);
		if (LoadedMesh == nullptr || LoadedMesh->GetRenderData() == nullptr)
		{
			return false;
		}

		const FString              ModelPath         = GetModelFilePath(ObjPathFileName);
		const TArray<FString>      MaterialSlotNames = BuildMaterialSlotNames(LoadedMesh);
		TArray<FModelMaterialInfo> MaterialInfos;
		const bool                 bBuiltMaterialInfos = FObjManager::BuildModelMaterialInfosFromObj(
			ObjPathFileName,
			ModelPath,
			MaterialSlotNames,
			MaterialInfos);
		if (!bBuiltMaterialInfos)
		{
			UE_LOG("[FObjManager] Falling back to default embedded material metadata for cache build: %s", ObjPathFileName.c_str());
		}

		const uint64 SourceTimestamp = GetFileWriteTimestamp(FPaths::ToPath(FPaths::ToAbsolutePath(ObjPathFileName)).lexically_normal());
		return FObjManager::SaveModelStaticMeshAsset(ModelPath, *LoadedMesh->GetRenderData(), MaterialInfos, SourceTimestamp, &LoadOptions);
	}

	UStaticMesh* FinalizeStaticMeshAsset(
		const FString&                    PathFileName,
		std::unique_ptr<FStaticMesh>      RawData,
		const TArray<FModelMaterialInfo>& MaterialInfos)
	{
		FString JustFileName = FPaths::FromPath(FPaths::ToPath(PathFileName).filename());

		RawData->PathFileName = JustFileName;
		RawData->UpdateLocalBound();

		const FVector MeshCenter = RawData->GetCenterCoord();
		for (FVertex& Vertex : RawData->Vertices)
		{
			Vertex.Position.X -= MeshCenter.X;
			Vertex.Position.Y -= MeshCenter.Y;
			Vertex.Position.Z -= MeshCenter.Z;
		}
		RawData->UpdateLocalBound();

		if (ShouldAutoSmoothNormals(MaterialInfos))
		{
			CalculateSmoothNormals(*RawData);
		}

		CalculateTangents(*RawData);

		UStaticMesh* NewAsset = FObjectFactory::ConstructObject<UStaticMesh>(nullptr, JustFileName);
		NewAsset->SetStaticMeshAsset(RawData.release());

		NewAsset->LocalBounds.Radius    = NewAsset->GetRenderData()->GetLocalBoundRadius();
		NewAsset->LocalBounds.Center    = NewAsset->GetRenderData()->GetCenterCoord();
		NewAsset->LocalBounds.BoxExtent = (NewAsset->GetRenderData()->GetMaxCoord() - NewAsset->GetRenderData()->GetMinCoord()) * 0.5f;

		uint32 SlotCount = GetRequiredMaterialSlotCount(*NewAsset->GetRenderData(), MaterialInfos);
		if (SlotCount == 0)
		{
			SlotCount = 1;
		}

		const std::filesystem::path ModelPath = FPaths::ToPath(FPaths::ToAbsolutePath(PathFileName)).lexically_normal();
		for (uint32 SlotIndex = 0; SlotIndex < SlotCount; ++SlotIndex)
		{
			const FModelMaterialInfo MaterialInfo = GetMaterialInfoOrDefault(MaterialInfos, SlotIndex);

			std::shared_ptr<FMaterial> Material = CreateImportedMaterialTemplate(MaterialInfo.Name);
			ApplyBaseColorToMaterial(Material, MaterialInfo.BaseColor);

			if (!MaterialInfo.DiffuseTexturePath.empty())
			{
				const std::filesystem::path TexturePath = ResolveTextureReferencePath(ModelPath, MaterialInfo.DiffuseTexturePath);
				if (!TryLoadTextureIntoMaterial(Material, TexturePath, "[.Model Loader] Auto-loaded texture-backed pixel shader:"))
				{
					UE_LOG("[.Model Loader] Failed to resolve embedded texture '%s' for material '%s'.",
					       MaterialInfo.DiffuseTexturePath.c_str(),
					       MaterialInfo.Name.c_str());
				}
			}
			if (!MaterialInfo.NormalTexturePath.empty())
			{
				const std::filesystem::path TexturePath = ResolveTextureReferencePath(ModelPath, MaterialInfo.NormalTexturePath);
				if (!TryLoadNormalTextureIntoMaterial(Material, TexturePath, "[.Model Loader] Auto-loaded normal map:"))
				{
					UE_LOG("[.Model Loader] Failed to resolve embedded normal map '%s' for material '%s'.",
						MaterialInfo.NormalTexturePath.c_str(),
						MaterialInfo.Name.c_str());
				}
			}
			if (!MaterialInfo.EmissiveTexturePath.empty())
			{
				const std::filesystem::path TexturePath = ResolveTextureReferencePath(ModelPath, MaterialInfo.EmissiveTexturePath);
				if (!TryLoadEmissiveTextureIntoMaterial(Material, TexturePath, "[.Model Loader] Auto-loaded emissive map:"))
				{
					UE_LOG("[.Model Loader] Failed to resolve embedded emissive map '%s' for material '%s'.",
						MaterialInfo.EmissiveTexturePath.c_str(),
						MaterialInfo.Name.c_str());
				}
			}

			if (!Material)
			{
				Material = FMaterialManager::Get().FindByName("M_Default");
			}

			NewAsset->AddDefaultMaterial(Material);
		}
		NewAsset->BuildAccelerationStructureIfNeeded();

		LoadAvailableLODs(*NewAsset, PathFileName);
		return NewAsset;
	}

	struct FObjParserContext
	{
		FStaticMesh*     OutMesh = nullptr;
		TArray<FString>& OutMaterialNames;

		TArray<FVector>        TempPositions;
		TArray<FVector2>       TempUVs;
		TArray<FVector>        TempNormals;
		const FObjLoadOptions& LoadOptions;

		struct FIndex
		{
			uint32 PositionIndex;
			uint32 UVIndex;
			uint32 NormalIndex;

			bool operator<(const FIndex& Other) const
			{
				if (PositionIndex != Other.PositionIndex)
				{
					return PositionIndex < Other.PositionIndex;
				}
				if (UVIndex != Other.UVIndex)
				{
					return UVIndex < Other.UVIndex;
				}
				return NormalIndex < Other.NormalIndex;
			}
		};

		std::map<FIndex, uint32> VertexCache;

		uint32 CurrentSectionStartIndex = 0;
		int32  CurrentMaterialIndex     = -1;

		FObjParserContext(FStaticMesh* InOutMesh, TArray<FString>& InOutMaterialNames, const FObjLoadOptions& InLoadOptions)
			: OutMesh(InOutMesh)
			  , OutMaterialNames(InOutMaterialNames)
			  , LoadOptions(InLoadOptions)
		{
		}

		void CloseCurrentSection()
		{
			if (OutMesh->Indices.size() > CurrentSectionStartIndex)
			{
				FMeshSection Section {};
				Section.MaterialIndex = static_cast<uint32>(CurrentMaterialIndex);
				Section.StartIndex    = CurrentSectionStartIndex;
				Section.IndexCount    = static_cast<uint32>(OutMesh->Indices.size()) - CurrentSectionStartIndex;
				OutMesh->Sections.push_back(Section);
				CurrentSectionStartIndex = static_cast<uint32>(OutMesh->Indices.size());
			}
		}

		void ParseUseMtl(std::stringstream& SS)
		{
			std::string MaterialName;
			SS >> MaterialName;

			CloseCurrentSection();

			CurrentMaterialIndex = static_cast<int32>(OutMaterialNames.size());
			OutMaterialNames.push_back(FString(MaterialName.c_str()));
		}

		void ParseFace(std::stringstream& SS)
		{
			if (CurrentMaterialIndex == -1)
			{
				CurrentMaterialIndex = 0;
				OutMaterialNames.push_back("M_Default");
			}

			std::string    VStr;
			TArray<FIndex> Face;

			while (SS >> VStr)
			{
				std::stringstream VSS(VStr);
				std::string       PositionString;
				std::string       UVString;
				std::string       NormalString;

				std::getline(VSS, PositionString, '/');
				std::getline(VSS, UVString, '/');
				std::getline(VSS, NormalString, '/');

				FIndex Idx {};
				Idx.PositionIndex = std::stoi(PositionString) - 1;
				Idx.UVIndex       = UVString.empty() ? -1 : std::stoi(UVString) - 1;
				Idx.NormalIndex   = NormalString.empty() ? -1 : std::stoi(NormalString) - 1;

				Face.push_back(Idx);
			}

			TArray<uint32> FaceIndices;

			for (const FIndex& Idx : Face)
			{
				auto It = VertexCache.find(Idx);
				if (It != VertexCache.end())
				{
					FaceIndices.push_back(It->second);
				}
				else
				{
					uint32 NewVertexIndex = static_cast<uint32>(OutMesh->Vertices.size());

					FVertex V {};
					V.Position = TempPositions[Idx.PositionIndex];
					V.Color    = FVector4(1.0f, 1.0f, 1.0f, 1.0f);

					if (!TempUVs.empty() && Idx.UVIndex < TempUVs.size())
					{
						V.UV = TempUVs[Idx.UVIndex];
					}
					if (!TempNormals.empty() && Idx.NormalIndex < TempNormals.size())
					{
						V.Normal = TempNormals[Idx.NormalIndex];
					}

					OutMesh->Vertices.push_back(V);

					VertexCache[Idx] = NewVertexIndex;
					FaceIndices.push_back(NewVertexIndex);
				}
			}

			for (size_t i = 1; i + 1 < FaceIndices.size(); ++i)
			{
				if (GetObjConversionDeterminantSign(LoadOptions) < 0)
				{
					OutMesh->Indices.push_back(FaceIndices[0]);
					OutMesh->Indices.push_back(FaceIndices[i + 1]);
					OutMesh->Indices.push_back(FaceIndices[i]);
				}
				else
				{
					OutMesh->Indices.push_back(FaceIndices[0]);
					OutMesh->Indices.push_back(FaceIndices[i]);
					OutMesh->Indices.push_back(FaceIndices[i + 1]);
				}
			}
		}
	};
}

UStaticMesh* FObjManager::LoadStaticMeshAsset(const FString& PathFileName)
{
	FString       StandardizedPath = GetStandardizedMeshPath(PathFileName);
	const FString Extension        = GetNormalizedExtension(StandardizedPath);
	if (Extension == ".obj" || Extension.empty())
	{
		FObjLoadOptions             PreferredLoadOptions {};
		const FString               ModelPath       = GetModelFilePath(StandardizedPath);
		const std::filesystem::path ObjFsPath       = FPaths::ToPath(FPaths::ToAbsolutePath(StandardizedPath)).lexically_normal();
		const std::filesystem::path ModelFsPath     = FPaths::ToPath(FPaths::ToAbsolutePath(ModelPath)).lexically_normal();
		const uint64                SourceTimestamp = GetFileWriteTimestamp(ObjFsPath);
		(void)FObjManager::ReadModelImportOptions(ModelPath, PreferredLoadOptions);

		uint64     CachedTimestamp          = 0;
		const bool bHasValidSourceTimestamp = (SourceTimestamp != 0);
		const bool bHasModelTimestamp       = ReadModelSourceTimestamp(ModelPath, CachedTimestamp);
		if (PathExists(ModelFsPath) && bHasValidSourceTimestamp && (!bHasModelTimestamp || !AreSourceTimestampsEquivalent(CachedTimestamp, SourceTimestamp)))
		{
			InvalidateCacheEntriesForAsset(StandardizedPath);
			RemoveModelArtifact(StandardizedPath);
		}

		if (PathExists(ModelFsPath))
		{
			if (UStaticMesh* CachedModel = LoadModelStaticMeshAsset(ModelPath))
			{
				return CachedModel;
			}

			InvalidateCacheEntriesForAsset(StandardizedPath);
			RemoveModelArtifact(StandardizedPath);
		}

		if (BuildModelCacheForObj(StandardizedPath, PreferredLoadOptions))
		{
			const FString ObjCacheKey = BuildObjCacheKey(StandardizedPath, PreferredLoadOptions);
			auto          ObjIt       = ObjStaticMeshMap.find(ObjCacheKey);
			if (ObjIt != ObjStaticMeshMap.end())
			{
				delete ObjIt->second;
				ObjStaticMeshMap.erase(ObjIt);
			}

			if (UStaticMesh* CachedModel = LoadModelStaticMeshAsset(ModelPath))
			{
				return CachedModel;
			}
		}

		return LoadObjStaticMeshAsset(StandardizedPath, PreferredLoadOptions);
	}

	if (Extension == ".model")
	{
		// Explicit .model loads should keep the baked mesh exactly as saved.
		// Rebuilding from a sibling .obj would discard export-time axis remapping.
		return LoadModelStaticMeshAsset(StandardizedPath);
	}

	UE_LOG("[FObjManager] Unsupported static mesh extension: %s", PathFileName.c_str());
	return nullptr;
}

UStaticMesh* FObjManager::LoadObjStaticMeshAsset(const FString& PathFileName)
{
	return LoadObjStaticMeshAsset(PathFileName, FObjLoadOptions {});
}

UStaticMesh* FObjManager::LoadObjStaticMeshAsset(const FString& PathFileName, const FObjLoadOptions& LoadOptions)
{
	const FString CacheKey = BuildObjCacheKey(PathFileName, LoadOptions);

	auto It = ObjStaticMeshMap.find(CacheKey);
	if (It != ObjStaticMeshMap.end())
	{
		if (It->second != nullptr)
		{
			LoadAvailableLODs(*It->second, GetStandardizedMeshPath(PathFileName));
		}
		return It->second;
	}

	auto            RawData = std::make_unique<FStaticMesh>();
	TArray<FString> FoundMaterials;
	if (!ParseObjFile(PathFileName, RawData.get(), FoundMaterials, LoadOptions))
	{
		return nullptr;
	}

	UStaticMesh* NewAsset      = FinalizeStaticMeshAsset(PathFileName, std::move(RawData), FoundMaterials);
	ObjStaticMeshMap[CacheKey] = NewAsset;
	return NewAsset;
}

UStaticMesh* FObjManager::LoadModelStaticMeshAsset(const FString& PathFileName)
{
	FString StandardizedPath = GetStandardizedMeshPath(PathFileName);

	auto It = ObjStaticMeshMap.find(StandardizedPath);
	if (It != ObjStaticMeshMap.end())
	{
		LoadAvailableLODs(*It->second, StandardizedPath);
		return It->second;
	}

	const FString               AbsolutePath = FPaths::ToAbsolutePath(PathFileName);
	const std::filesystem::path FilePath     = FPaths::ToPath(AbsolutePath).lexically_normal();

	std::ifstream File(FilePath, std::ios::binary);
	if (!File.is_open())
	{
		UE_LOG("[FObjManager] Failed to open .Model file: %s", AbsolutePath.c_str());
		return nullptr;
	}

	char Magic[sizeof(GModelMagic)] = {};
	if (!ReadBinaryBytes(File, Magic, sizeof(Magic)) || std::memcmp(Magic, GModelMagic, sizeof(GModelMagic)) != 0)
	{
		UE_LOG("[FObjManager] Invalid .Model header: %s", AbsolutePath.c_str());
		return nullptr;
	}

	uint32 Version           = 0;
	uint64 SourceTimestamp   = 0;
	uint32 VertexCount       = 0;
	uint32 IndexCount        = 0;
	uint32 SectionCount      = 0;
	uint32 MaterialSlotCount = 0;
	if (!ReadBinaryValue(File, Version))
	{
		UE_LOG("[FObjManager] Failed to read .Model header: %s", AbsolutePath.c_str());
		return nullptr;
	}

	if (Version >= GModelVersionSourceTimestamp)
	{
		if (!ReadBinaryValue(File, SourceTimestamp))
		{
			UE_LOG("[FObjManager] Failed to read .Model source timestamp: %s", AbsolutePath.c_str());
			return nullptr;
		}
	}

	if (Version >= GModelVersionNormalTexture)
	{
		FObjLoadOptions SavedLoadOptions {};
		bool            bHasSavedLoadOptions = false;
		if (!ReadModelImportOptionsPayload(File, SavedLoadOptions, bHasSavedLoadOptions))
		{
			UE_LOG("[FObjManager] Failed to read .Model import settings: %s", AbsolutePath.c_str());
			return nullptr;
		}
	}

	if (!ReadBinaryValue(File, VertexCount)
		|| !ReadBinaryValue(File, IndexCount)
		|| !ReadBinaryValue(File, SectionCount)
		|| !ReadBinaryValue(File, MaterialSlotCount))
	{
		UE_LOG("[FObjManager] Failed to read .Model header: %s", AbsolutePath.c_str());
		return nullptr;
	}

	if (Version < GModelVersionLegacy || Version > GModelVersion)
	{
		UE_LOG("[FObjManager] Unsupported .Model version %u: %s", Version, AbsolutePath.c_str());
		return nullptr;
	}

	auto RawData      = std::make_unique<FStaticMesh>();
	RawData->Topology = EMeshTopology::EMT_TriangleList;
	RawData->Vertices.resize(VertexCount);
	RawData->Indices.resize(IndexCount);
	RawData->Sections.resize(SectionCount);

	for (FVertex& Vertex : RawData->Vertices)
	{
		if (!ReadBinaryValue(File, Vertex.Position.X)
			|| !ReadBinaryValue(File, Vertex.Position.Y)
			|| !ReadBinaryValue(File, Vertex.Position.Z)
			|| !ReadBinaryValue(File, Vertex.Color.X)
			|| !ReadBinaryValue(File, Vertex.Color.Y)
			|| !ReadBinaryValue(File, Vertex.Color.Z)
			|| !ReadBinaryValue(File, Vertex.Color.W)
			|| !ReadBinaryValue(File, Vertex.Normal.X)
			|| !ReadBinaryValue(File, Vertex.Normal.Y)
			|| !ReadBinaryValue(File, Vertex.Normal.Z)
			|| !ReadBinaryValue(File, Vertex.UV.X)
			|| !ReadBinaryValue(File, Vertex.UV.Y))
		{
			UE_LOG("[FObjManager] Failed to read .Model vertices: %s", AbsolutePath.c_str());
			return nullptr;
		}
	}

	for (uint32& Index : RawData->Indices)
	{
		if (!ReadBinaryValue(File, Index))
		{
			UE_LOG("[FObjManager] Failed to read .Model indices: %s", AbsolutePath.c_str());
			return nullptr;
		}
	}

	for (FMeshSection& Section : RawData->Sections)
	{
		if (!ReadBinaryValue(File, Section.MaterialIndex)
			|| !ReadBinaryValue(File, Section.StartIndex)
			|| !ReadBinaryValue(File, Section.IndexCount))
		{
			UE_LOG("[FObjManager] Failed to read .Model sections: %s", AbsolutePath.c_str());
			return nullptr;
		}

		const uint64 SectionEndIndex = static_cast<uint64>(Section.StartIndex) + static_cast<uint64>(Section.IndexCount);
		if (SectionEndIndex > RawData->Indices.size())
		{
			UE_LOG("[FObjManager] Invalid .Model section range: %s", AbsolutePath.c_str());
			return nullptr;
		}
	}

	TArray<FString> MaterialSlotNames;
	MaterialSlotNames.resize(MaterialSlotCount);
	for (FString& MaterialSlotName : MaterialSlotNames)
	{
		if (!ReadUtf8String(File, MaterialSlotName))
		{
			UE_LOG("[FObjManager] Failed to read .Model material slots: %s", AbsolutePath.c_str());
			return nullptr;
		}
	}

	if (Version == GModelVersionLegacy)
	{
		UStaticMesh* NewAsset              = FinalizeStaticMeshAsset(PathFileName, std::move(RawData), MaterialSlotNames);
		ObjStaticMeshMap[StandardizedPath] = NewAsset;
		return NewAsset;
	}

	TArray<FModelMaterialInfo> MaterialInfos;
	MaterialInfos.resize(MaterialSlotCount);
	for (uint32 SlotIndex = 0; SlotIndex < MaterialSlotCount; ++SlotIndex)
	{
		FModelMaterialInfo& MaterialInfo = MaterialInfos[SlotIndex];
		MaterialInfo.Name                = GetMaterialSlotNameOrDefault(MaterialSlotNames, SlotIndex);

		if (!ReadBinaryValue(File, MaterialInfo.BaseColor.R)
			|| !ReadBinaryValue(File, MaterialInfo.BaseColor.G)
			|| !ReadBinaryValue(File, MaterialInfo.BaseColor.B)
			|| !ReadBinaryValue(File, MaterialInfo.BaseColor.A)
			|| !ReadUtf8String(File, MaterialInfo.DiffuseTexturePath)
			|| (Version >= GModelVersionNormalTexture && !ReadUtf8String(File, MaterialInfo.NormalTexturePath))
			|| (Version >= GModelVersionEmissiveTexture && !ReadUtf8String(File, MaterialInfo.EmissiveTexturePath)))
		{
			UE_LOG("[FObjManager] Failed to read .Model material metadata: %s", AbsolutePath.c_str());
			return nullptr;
		}
	}

	if (Version < GModelVersionEmissiveTexture)
	{
		const FString ObjPath = GetObjFilePathFromModelPath(PathFileName);
		const std::filesystem::path ObjFilePath = FPaths::ToPath(FPaths::ToAbsolutePath(ObjPath)).lexically_normal();
		if (PathExists(ObjFilePath))
		{
			TArray<FModelMaterialInfo> RebuiltInfos;
			if (BuildModelMaterialInfosFromObj(ObjPath, PathFileName, MaterialSlotNames, RebuiltInfos))
			{
				for (uint32 SlotIndex = 0; SlotIndex < MaterialInfos.size() && SlotIndex < RebuiltInfos.size(); ++SlotIndex)
				{
					if (MaterialInfos[SlotIndex].EmissiveTexturePath.empty())
					{
						MaterialInfos[SlotIndex].EmissiveTexturePath = RebuiltInfos[SlotIndex].EmissiveTexturePath;
					}
				}
			}
		}
	}

	UStaticMesh* NewAsset              = FinalizeStaticMeshAsset(PathFileName, std::move(RawData), MaterialInfos);
	ObjStaticMeshMap[StandardizedPath] = NewAsset;
	return NewAsset;
}

bool FObjManager::ReadModelImportOptions(const FString& PathFileName, FObjLoadOptions& OutLoadOptions)
{
	OutLoadOptions                    = FObjLoadOptions {};
	const FString               AbsolutePath = FPaths::ToAbsolutePath(PathFileName);
	const std::filesystem::path FilePath     = FPaths::ToPath(AbsolutePath).lexically_normal();

	std::ifstream File(FilePath, std::ios::binary);
	if (!File.is_open())
	{
		return false;
	}

	char   Magic[sizeof(GModelMagic)] = {};
	uint32 Version                    = 0;
	if (!ReadBinaryBytes(File, Magic, sizeof(Magic))
		|| !ReadBinaryValue(File, Version)
		|| std::memcmp(Magic, GModelMagic, sizeof(GModelMagic)) != 0)
	{
		return false;
	}

	if (Version < GModelVersionNormalTexture)
	{
		return false;
	}

	if (Version >= GModelVersionSourceTimestamp)
	{
		uint64 SourceTimestamp = 0;
		if (!ReadBinaryValue(File, SourceTimestamp))
		{
			return false;
		}
	}

	bool bHasLoadOptions = false;
	return ReadModelImportOptionsPayload(File, OutLoadOptions, bHasLoadOptions) && bHasLoadOptions;
}

FStaticMesh* FObjManager::LoadLodAsset(const FString& PathFileName, float* OutDistance)
{
	const FString               AbsolutePath = FPaths::ToAbsolutePath(PathFileName);
	const std::filesystem::path FilePath     = FPaths::ToPath(AbsolutePath).lexically_normal();

	std::ifstream File(FilePath, std::ios::binary);
	if (!File.is_open())
	{
		return nullptr;
	}

	char Magic[sizeof(GLODMagic)] = {};
	if (!ReadBinaryBytes(File, Magic, sizeof(Magic)) || std::memcmp(Magic, GLODMagic, sizeof(GLODMagic)) != 0)
	{
		UE_LOG("[FObjManager] Invalid .lod header: %s", AbsolutePath.c_str());
		return nullptr;
	}

	uint32 Version         = 0;
	uint64 SourceTimestamp = 0;
	float  Distance        = 0.0f;
	uint32 VertexCount     = 0;
	uint32 IndexCount      = 0;
	uint32 SectionCount    = 0;
	if (!ReadBinaryValue(File, Version))
	{
		UE_LOG("[FObjManager] Failed to read .lod header: %s", AbsolutePath.c_str());
		return nullptr;
	}

	if (Version >= GLODVersionSourceTimestamp)
	{
		if (!ReadBinaryValue(File, SourceTimestamp))
		{
			UE_LOG("[FObjManager] Failed to read .lod source timestamp: %s", AbsolutePath.c_str());
			return nullptr;
		}
	}

	if (Version >= GLODVersionDistance)
	{
		if (!ReadBinaryValue(File, Distance))
		{
			UE_LOG("[FObjManager] Failed to read .lod distance: %s", AbsolutePath.c_str());
			return nullptr;
		}
	}
	else if (Version >= GLODVersionScreenSize)
	{
		float LegacyScreenSize = 0.0f;
		if (!ReadBinaryValue(File, LegacyScreenSize))
		{
			UE_LOG("[FObjManager] Failed to read .lod screen size: %s", AbsolutePath.c_str());
			return nullptr;
		}
		Distance = LegacyScreenSize;
	}

	if (!ReadBinaryValue(File, VertexCount)
		|| !ReadBinaryValue(File, IndexCount)
		|| !ReadBinaryValue(File, SectionCount))
	{
		UE_LOG("[FObjManager] Failed to read .lod header: %s", AbsolutePath.c_str());
		return nullptr;
	}

	if (Version != GLODVersionLegacy && Version != GLODVersionSourceTimestamp && Version != GLODVersionScreenSize && Version != GLODVersionDistance)
	{
		UE_LOG("[FObjManager] Unsupported .lod version %u: %s", Version, AbsolutePath.c_str());
		return nullptr;
	}

	auto Mesh      = std::make_unique<FStaticMesh>();
	Mesh->Topology = EMeshTopology::EMT_TriangleList;
	Mesh->Vertices.resize(VertexCount);
	Mesh->Indices.resize(IndexCount);
	Mesh->Sections.resize(SectionCount);

	for (FVertex& Vertex : Mesh->Vertices)
	{
		if (!ReadBinaryValue(File, Vertex.Position.X)
			|| !ReadBinaryValue(File, Vertex.Position.Y)
			|| !ReadBinaryValue(File, Vertex.Position.Z)
			|| !ReadBinaryValue(File, Vertex.Color.X)
			|| !ReadBinaryValue(File, Vertex.Color.Y)
			|| !ReadBinaryValue(File, Vertex.Color.Z)
			|| !ReadBinaryValue(File, Vertex.Color.W)
			|| !ReadBinaryValue(File, Vertex.Normal.X)
			|| !ReadBinaryValue(File, Vertex.Normal.Y)
			|| !ReadBinaryValue(File, Vertex.Normal.Z)
			|| !ReadBinaryValue(File, Vertex.UV.X)
			|| !ReadBinaryValue(File, Vertex.UV.Y))
		{
			UE_LOG("[FObjManager] Failed to read .lod vertices: %s", AbsolutePath.c_str());
			return nullptr;
		}
	}

	for (uint32& Index : Mesh->Indices)
	{
		if (!ReadBinaryValue(File, Index))
		{
			UE_LOG("[FObjManager] Failed to read .lod indices: %s", AbsolutePath.c_str());
			return nullptr;
		}
	}

	for (FMeshSection& Section : Mesh->Sections)
	{
		if (!ReadBinaryValue(File, Section.MaterialIndex)
			|| !ReadBinaryValue(File, Section.StartIndex)
			|| !ReadBinaryValue(File, Section.IndexCount))
		{
			UE_LOG("[FObjManager] Failed to read .lod sections: %s", AbsolutePath.c_str());
			return nullptr;
		}

		const uint64 SectionEndIndex = static_cast<uint64>(Section.StartIndex) + static_cast<uint64>(Section.IndexCount);
		if (SectionEndIndex > Mesh->Indices.size())
		{
			UE_LOG("[FObjManager] Invalid .lod section range: %s", AbsolutePath.c_str());
			return nullptr;
		}
	}

	Mesh->UpdateLocalBound();
	Mesh->bIsDirty = true;
	if (OutDistance != nullptr && Version >= GLODVersionDistance)
	{
		*OutDistance = Distance;
	}
	return Mesh.release();
}

bool FObjManager::SaveModelStaticMeshAsset(const FString& PathFileName, const FStaticMesh& StaticMesh, const TArray<FModelMaterialInfo>& MaterialInfos, uint64 SourceTimestamp, const FObjLoadOptions* LoadOptions)
{
	if (StaticMesh.Topology != EMeshTopology::EMT_TriangleList)
	{
		UE_LOG("[FObjManager] Only triangle-list meshes can be exported as .Model: %s", PathFileName.c_str());
		return false;
	}

	const FString               AbsolutePath = FPaths::ToAbsolutePath(PathFileName);
	const std::filesystem::path FilePath     = FPaths::ToPath(AbsolutePath).lexically_normal();

	std::error_code ErrorCode;
	if (!FilePath.parent_path().empty())
	{
		std::filesystem::create_directories(FilePath.parent_path(), ErrorCode);
	}

	std::ofstream File(FilePath, std::ios::binary | std::ios::trunc);
	if (!File.is_open())
	{
		UE_LOG("[FObjManager] Failed to create .Model file: %s", AbsolutePath.c_str());
		return false;
	}

	uint32 MaterialSlotCount = GetRequiredMaterialSlotCount(StaticMesh, MaterialInfos);
	if (MaterialSlotCount == 0)
	{
		MaterialSlotCount = 1;
	}

	if (!WriteBinaryBytes(File, GModelMagic, sizeof(GModelMagic))
		|| !WriteBinaryValue(File, GModelVersion)
		|| !WriteBinaryValue(File, SourceTimestamp)
		|| !WriteModelImportOptionsPayload(File, LoadOptions)
		|| !WriteBinaryValue(File, static_cast<uint32>(StaticMesh.Vertices.size()))
		|| !WriteBinaryValue(File, static_cast<uint32>(StaticMesh.Indices.size()))
		|| !WriteBinaryValue(File, static_cast<uint32>(StaticMesh.Sections.size()))
		|| !WriteBinaryValue(File, MaterialSlotCount))
	{
		return false;
	}

	for (const FVertex& Vertex : StaticMesh.Vertices)
	{
		if (!WriteBinaryValue(File, Vertex.Position.X)
			|| !WriteBinaryValue(File, Vertex.Position.Y)
			|| !WriteBinaryValue(File, Vertex.Position.Z)
			|| !WriteBinaryValue(File, Vertex.Color.X)
			|| !WriteBinaryValue(File, Vertex.Color.Y)
			|| !WriteBinaryValue(File, Vertex.Color.Z)
			|| !WriteBinaryValue(File, Vertex.Color.W)
			|| !WriteBinaryValue(File, Vertex.Normal.X)
			|| !WriteBinaryValue(File, Vertex.Normal.Y)
			|| !WriteBinaryValue(File, Vertex.Normal.Z)
			|| !WriteBinaryValue(File, Vertex.UV.X)
			|| !WriteBinaryValue(File, Vertex.UV.Y))
		{
			return false;
		}
	}

	for (uint32 Index : StaticMesh.Indices)
	{
		if (!WriteBinaryValue(File, Index))
		{
			return false;
		}
	}

	for (const FMeshSection& Section : StaticMesh.Sections)
	{
		if (!WriteBinaryValue(File, Section.MaterialIndex)
			|| !WriteBinaryValue(File, Section.StartIndex)
			|| !WriteBinaryValue(File, Section.IndexCount))
		{
			return false;
		}
	}

	for (uint32 SlotIndex = 0; SlotIndex < MaterialSlotCount; ++SlotIndex)
	{
		const FModelMaterialInfo MaterialInfo = GetMaterialInfoOrDefault(MaterialInfos, SlotIndex);
		if (!WriteUtf8String(File, MaterialInfo.Name))
		{
			return false;
		}
	}

	for (uint32 SlotIndex = 0; SlotIndex < MaterialSlotCount; ++SlotIndex)
	{
		const FModelMaterialInfo MaterialInfo = GetMaterialInfoOrDefault(MaterialInfos, SlotIndex);
		if (!WriteBinaryValue(File, MaterialInfo.BaseColor.R)
			|| !WriteBinaryValue(File, MaterialInfo.BaseColor.G)
			|| !WriteBinaryValue(File, MaterialInfo.BaseColor.B)
			|| !WriteBinaryValue(File, MaterialInfo.BaseColor.A)
			|| !WriteUtf8String(File, MaterialInfo.DiffuseTexturePath)
			|| !WriteUtf8String(File, MaterialInfo.NormalTexturePath)
			|| !WriteUtf8String(File, MaterialInfo.EmissiveTexturePath))
		{
			return false;
		}
	}

	return File.good();
}

bool FObjManager::SaveLodAsset(const FString& PathFileName, const FStaticMesh& LodMesh, uint64 SourceTimestamp, float Distance)
{
	const FString               AbsolutePath = FPaths::ToAbsolutePath(PathFileName);
	const std::filesystem::path FilePath     = FPaths::ToPath(AbsolutePath).lexically_normal();

	std::error_code ErrorCode;
	if (!FilePath.parent_path().empty())
	{
		std::filesystem::create_directories(FilePath.parent_path(), ErrorCode);
	}

	std::ofstream File(FilePath, std::ios::binary | std::ios::trunc);
	if (!File.is_open())
	{
		UE_LOG("[FObjManager] Failed to create .lod file: %s", AbsolutePath.c_str());
		return false;
	}

	if (!WriteBinaryBytes(File, GLODMagic, sizeof(GLODMagic))
		|| !WriteBinaryValue(File, GLODVersion)
		|| !WriteBinaryValue(File, SourceTimestamp)
		|| !WriteBinaryValue(File, Distance)
		|| !WriteBinaryValue(File, static_cast<uint32>(LodMesh.Vertices.size()))
		|| !WriteBinaryValue(File, static_cast<uint32>(LodMesh.Indices.size()))
		|| !WriteBinaryValue(File, static_cast<uint32>(LodMesh.Sections.size())))
	{
		return false;
	}

	for (const FVertex& Vertex : LodMesh.Vertices)
	{
		if (!WriteBinaryValue(File, Vertex.Position.X)
			|| !WriteBinaryValue(File, Vertex.Position.Y)
			|| !WriteBinaryValue(File, Vertex.Position.Z)
			|| !WriteBinaryValue(File, Vertex.Color.X)
			|| !WriteBinaryValue(File, Vertex.Color.Y)
			|| !WriteBinaryValue(File, Vertex.Color.Z)
			|| !WriteBinaryValue(File, Vertex.Color.W)
			|| !WriteBinaryValue(File, Vertex.Normal.X)
			|| !WriteBinaryValue(File, Vertex.Normal.Y)
			|| !WriteBinaryValue(File, Vertex.Normal.Z)
			|| !WriteBinaryValue(File, Vertex.UV.X)
			|| !WriteBinaryValue(File, Vertex.UV.Y))
		{
			return false;
		}
	}

	for (uint32 Index : LodMesh.Indices)
	{
		if (!WriteBinaryValue(File, Index))
		{
			return false;
		}
	}

	for (const FMeshSection& Section : LodMesh.Sections)
	{
		if (!WriteBinaryValue(File, Section.MaterialIndex)
			|| !WriteBinaryValue(File, Section.StartIndex)
			|| !WriteBinaryValue(File, Section.IndexCount))
		{
			return false;
		}
	}

	return File.good();
}

bool FObjManager::BuildModelMaterialInfosFromObj(
	const FString&              ObjFilePath,
	const FString&              ModelFilePath,
	const TArray<FString>&      MaterialSlotNames,
	TArray<FModelMaterialInfo>& OutMaterialInfos)
{
	const uint32 SlotCount = (std::max)(1u, static_cast<uint32>(MaterialSlotNames.size()));
	OutMaterialInfos.clear();
	OutMaterialInfos.resize(SlotCount);
	for (uint32 SlotIndex = 0; SlotIndex < SlotCount; ++SlotIndex)
	{
		OutMaterialInfos[SlotIndex].Name = GetMaterialSlotNameOrDefault(MaterialSlotNames, SlotIndex);
	}

	const FString               AbsoluteObjPath   = FPaths::ToAbsolutePath(ObjFilePath);
	const FString               AbsoluteModelPath = FPaths::ToAbsolutePath(ModelFilePath);
	const std::filesystem::path ObjPath           = FPaths::ToPath(AbsoluteObjPath).lexically_normal();
	const std::filesystem::path ModelPath         = FPaths::ToPath(AbsoluteModelPath).lexically_normal();

	std::ifstream ObjFile(ObjPath);
	if (!ObjFile.is_open())
	{
		UE_LOG("[FObjManager] Failed to open OBJ while collecting .Model material data: %s", AbsoluteObjPath.c_str());
		return false;
	}

	struct FParsedMaterialData
	{
		FLinearColor BaseColor = FLinearColor::White;
		FString  DiffuseTexturePath;
		FString  NormalTexturePath;
		FString  EmissiveTexturePath;
	};

	TMap<FString, FParsedMaterialData> ParsedMaterials;
	FString                            ObjLine;
	while (std::getline(ObjFile, ObjLine))
	{
		if (ObjLine.empty() || ObjLine[0] == '#')
		{
			continue;
		}

		std::stringstream ObjSS(ObjLine);
		FString           Type;
		ObjSS >> Type;
		if (Type != "mtllib")
		{
			continue;
		}

		std::string MaterialReferenceRaw;
		std::getline(ObjSS, MaterialReferenceRaw);
		FString MaterialReference = ObjFileStringToUtf8(TrimAscii(MaterialReferenceRaw));
		if (MaterialReference.empty())
		{
			continue;
		}

		const std::filesystem::path MtlPath = ResolveMaterialReferencePath(ObjPath, MaterialReference);
		std::ifstream               MtlFile(MtlPath);
		if (!MtlFile.is_open())
		{
			const FString MtlPathUtf8 = FPaths::FromPath(MtlPath);
			UE_LOG("[FObjManager] Failed to open MTL while collecting .Model material data: %s", MtlPathUtf8.c_str());
			continue;
		}

		FString CurrentMaterialName;
		FString MtlLine;
		while (std::getline(MtlFile, MtlLine))
		{
			if (MtlLine.empty() || MtlLine[0] == '#')
			{
				continue;
			}

			std::stringstream MtlSS(MtlLine);
			FString           MtlType;
			MtlSS >> MtlType;

			if (MtlType == "newmtl")
			{
				MtlSS >> CurrentMaterialName;
				if (!CurrentMaterialName.empty())
				{
					ParsedMaterials.try_emplace(CurrentMaterialName, FParsedMaterialData {});
				}
			}
			else if (MtlType == "Kd" && !CurrentMaterialName.empty())
			{
				float R = 1.0f;
				float G = 1.0f;
				float B = 1.0f;
				MtlSS >> R >> G >> B;
				ParsedMaterials[CurrentMaterialName].BaseColor = FLinearColor(R, G, B, 1.0f);
			}
			else if (MtlType == "map_Kd" && !CurrentMaterialName.empty())
			{
				std::string TextureReferenceRaw;
				std::getline(MtlSS, TextureReferenceRaw);
				FString TextureReference = ExtractTextureReferenceFromMtlStatement(TextureReferenceRaw);
				if (TextureReference.empty())
				{
					continue;
				}

				const std::filesystem::path TexturePath = ResolveTextureReferencePath(MtlPath, TextureReference);
				if (PathExists(TexturePath))
				{
					ParsedMaterials[CurrentMaterialName].DiffuseTexturePath = MakeStoredTexturePath(ModelPath, TexturePath);
				}
				else
				{
					UE_LOG("[FObjManager] Failed to resolve MTL texture '%s' for material '%s'.",
					       TextureReference.c_str(),
					       CurrentMaterialName.c_str());
				}
			}
			else if ((MtlType == "map_Bump" || MtlType == "bump") && !CurrentMaterialName.empty())
			{
				std::string TextureReferenceRaw;
				std::getline(MtlSS, TextureReferenceRaw);
				FString TextureReference = ExtractTextureReferenceFromMtlStatement(TextureReferenceRaw);
				if (TextureReference.empty())
				{
					continue;
				}

				const std::filesystem::path TexturePath = ResolveTextureReferencePath(MtlPath, TextureReference);
				if (PathExists(TexturePath))
				{
					ParsedMaterials[CurrentMaterialName].NormalTexturePath = MakeStoredTexturePath(ModelPath, TexturePath);
				}
				else
				{
					UE_LOG("[FObjManager] Failed to resolve MTL texture '%s' for material '%s'.",
						TextureReference.c_str(),
						CurrentMaterialName.c_str());
				}
			}
			else if (MtlType == "map_Ke" && !CurrentMaterialName.empty())
			{
				std::string TextureReferenceRaw;
				std::getline(MtlSS, TextureReferenceRaw);
				FString TextureReference = ExtractTextureReferenceFromMtlStatement(TextureReferenceRaw);
				if (TextureReference.empty())
				{
					continue;
				}

				const std::filesystem::path TexturePath = ResolveTextureReferencePath(MtlPath, TextureReference);
				if (PathExists(TexturePath))
				{
					ParsedMaterials[CurrentMaterialName].EmissiveTexturePath = MakeStoredTexturePath(ModelPath, TexturePath);
				}
				else
				{
					UE_LOG("[FObjManager] Failed to resolve MTL texture '%s' for material '%s'.",
						TextureReference.c_str(),
						CurrentMaterialName.c_str());
				}
			}
		}
	}

	for (FModelMaterialInfo& MaterialInfo : OutMaterialInfos)
	{
		auto It = ParsedMaterials.find(MaterialInfo.Name);
		if (It != ParsedMaterials.end())
		{
			MaterialInfo.BaseColor          = It->second.BaseColor;
			MaterialInfo.DiffuseTexturePath = It->second.DiffuseTexturePath;
			MaterialInfo.NormalTexturePath = It->second.NormalTexturePath;
			MaterialInfo.EmissiveTexturePath = It->second.EmissiveTexturePath;
		}
	}

	return true;
}

bool FObjManager::ParseMtlFile(const FString& MtlFIlePath)
{
	const FString               AbsolutePath = FPaths::ToAbsolutePath(MtlFIlePath);
	const std::filesystem::path FilePath     = FPaths::ToPath(AbsolutePath).lexically_normal();

	std::ifstream File(FilePath);
	if (!File.is_open())
	{
		return false;
	}

	std::string                Line;
	std::shared_ptr<FMaterial> CurrentMaterial = nullptr;

	while (std::getline(File, Line))
	{
		if (Line.empty() || Line[0] == '#')
		{
			continue;
		}

		std::stringstream SS(Line);
		std::string       Type;
		SS >> Type;

		if (Type == "newmtl")
		{
			std::string MaterialName;
			SS >> MaterialName;

			CurrentMaterial = CreateImportedMaterialTemplate(MaterialName.c_str());
			FMaterialManager::Get().Register(MaterialName.c_str(), CurrentMaterial);
		}
		else if (Type == "Kd" && CurrentMaterial)
		{
			float R = 0.0f;
			float G = 0.0f;
			float B = 0.0f;
			SS >> R >> G >> B;

			ApplyBaseColorToMaterial(CurrentMaterial, FLinearColor(R, G, B, 1.0f));
		}
		else if (Type == "Ke" && CurrentMaterial)
		{
			float R = 0.0f;
			float G = 0.0f;
			float B = 0.0f;
			SS >> R >> G >> B;

			CurrentMaterial->SetLinearColorParameter("EmissiveColor", FLinearColor(R, G, B, 1.0f));
		}
		else if (Type == "map_Kd" && CurrentMaterial)
		{
			std::string TextureReferenceRaw;
			std::getline(SS, TextureReferenceRaw);
			FString TextureReference = ExtractTextureReferenceFromMtlStatement(TextureReferenceRaw);

			const std::filesystem::path TexturePath = ResolveTextureReferencePath(FilePath, TextureReference);
			if (!TryLoadTextureIntoMaterial(CurrentMaterial, TexturePath, "[MTL Parser] Auto-loaded texture-backed pixel shader:"))
			{
				UE_LOG("[MTL Parser] Failed to resolve texture '%s' referenced by '%s'.",
				       TextureReference.c_str(),
				       AbsolutePath.c_str());
			}
		}
		else if ((Type == "map_Bump" || Type == "bump") && CurrentMaterial)
		{
			std::string TextureReferenceRaw;
			std::getline(SS, TextureReferenceRaw);
			FString TextureReference = ExtractTextureReferenceFromMtlStatement(TextureReferenceRaw);

			const std::filesystem::path TexturePath = ResolveTextureReferencePath(FilePath, TextureReference);
			if (!TryLoadNormalTextureIntoMaterial(CurrentMaterial, TexturePath, "[MTL Parser] Auto-loaded normal map:"))
			{
				UE_LOG("[MTL Parser] Failed to resolve normal map '%s' referenced by '%s'.",
					TextureReference.c_str(),
					AbsolutePath.c_str());
			}
		}
		else if (Type == "map_Ke" && CurrentMaterial)
		{
			std::string TextureReferenceRaw;
			std::getline(SS, TextureReferenceRaw);
			FString TextureReference = ExtractTextureReferenceFromMtlStatement(TextureReferenceRaw);

			const std::filesystem::path TexturePath = ResolveTextureReferencePath(FilePath, TextureReference);
			if (!TryLoadEmissiveTextureIntoMaterial(CurrentMaterial, TexturePath, "[MTL Parser] Auto-loaded emissive map:"))
			{
				UE_LOG("[MTL Parser] Failed to resolve emissive map '%s' referenced by '%s'.",
					TextureReference.c_str(),
					AbsolutePath.c_str());
			}
		}
	}

	return true;
}

void FObjManager::PreloadAllObjFiles(const FString& DirectoryPath)
{
	const FString               AbsolutePath = FPaths::ToAbsolutePath(DirectoryPath);
	const std::filesystem::path DirPath      = FPaths::ToPath(AbsolutePath).lexically_normal();

	// 전달된 경로가 실제 디렉터리인지 확인한다.
	if (!std::filesystem::exists(DirPath) || !std::filesystem::is_directory(DirPath))
	{
		UE_LOG("[FObjManager] Preload 실패: 디렉터리를 찾을 수 없습니다. (%s)", AbsolutePath.c_str());
		return;
	}

	for (const auto& Entry : std::filesystem::directory_iterator(DirPath))
	{
		if (Entry.is_regular_file() && Entry.path().extension() == ".obj")
		{
			FString FullFilePath = FPaths::FromPath(Entry.path());

			UStaticMesh* LoadedMesh = LoadObjStaticMeshAsset(FullFilePath.c_str());
		}
	}
}

void FObjManager::PreloadAllModelFiles(const FString& DirectoryPath)
{
	const FString               AbsolutePath = FPaths::ToAbsolutePath(DirectoryPath);
	const std::filesystem::path DirPath      = FPaths::ToPath(AbsolutePath).lexically_normal();

	// 전달된 경로가 실제 디렉터리인지 확인한다.
	if (!std::filesystem::exists(DirPath) || !std::filesystem::is_directory(DirPath))
	{
		UE_LOG("[FObjManager] Preload 실패: 디렉터리를 찾을 수 없습니다. (%s)", AbsolutePath.c_str());
		return;
	}

	for (const auto& Entry : std::filesystem::directory_iterator(DirPath))
	{
		if (Entry.is_regular_file() && GetNormalizedExtension(FPaths::FromPath(Entry.path())) == ".model")
		{
			FString FullFilePath = FPaths::FromPath(Entry.path());

			UStaticMesh* LoadedMesh = LoadStaticMeshAsset(FullFilePath.c_str());
		}
	}
	PreloadAllMtlFiles(FPaths::FromPath(FPaths::MaterialDir()).c_str());
}

void FObjManager::PreloadAllMtlFiles(const FString& DirectoryPath)
{
	const FString               AbsolutePath = FPaths::ToAbsolutePath(DirectoryPath);
	const std::filesystem::path DirPath      = FPaths::ToPath(AbsolutePath).lexically_normal();

	if (!std::filesystem::exists(DirPath) || !std::filesystem::is_directory(DirPath))
	{
		UE_LOG("[FObjManager] MTL Preload 실패: 디렉터리를 찾을 수 없습니다. (%s)", AbsolutePath.c_str());
		return;
	}

	for (const auto& Entry : std::filesystem::directory_iterator(DirPath))
	{
		if (Entry.is_regular_file() && GetNormalizedExtension(FPaths::FromPath(Entry.path())) == ".mtl")
		{
			FString FullFilePath = FPaths::FromPath(Entry.path());
			ParseMtlFile(FullFilePath.c_str());
		}
	}
}

void FObjManager::ClearCache()
{
	for (auto& [PathName, Asset] : ObjStaticMeshMap)
	{
		if (Asset != nullptr)
		{
			delete Asset;
			Asset = nullptr;
		}
	}

	ObjStaticMeshMap.clear();
}

bool FObjManager::ParseObjFile(const FString& FilePath, FStaticMesh* OutMesh, TArray<FString>& OutMaterialNames, const FObjLoadOptions& LoadOptions)
{
	const FString               AbsolutePath = FPaths::ToAbsolutePath(FilePath);
	const std::filesystem::path ObjPath      = FPaths::ToPath(AbsolutePath).lexically_normal();

	if (!LoadOptions.bUseLegacyObjConversion &&
		GetAxisBaseIndex(LoadOptions.ForwardAxis) == GetAxisBaseIndex(LoadOptions.UpAxis))
	{
		UE_LOG("[FObjManager] Invalid OBJ axis conversion pair for file: %s", AbsolutePath.c_str());
		return false;
	}

	std::ifstream File(ObjPath);
	if (!File.is_open())
	{
		UE_LOG("[FObjManager] Failed to open OBJ file: %s", AbsolutePath.c_str());
		return false;
	}

	FObjParserContext Context(OutMesh, OutMaterialNames, LoadOptions);
	std::string       Line;

	while (std::getline(File, Line))
	{
		if (Line.empty() || Line[0] == '#')
		{
			continue;
		}

		std::stringstream SS(Line);
		std::string       Type;
		SS >> Type;

		if (Type == "mtllib")
		{
			std::string MtlFileName;
			SS >> MtlFileName;

			const std::filesystem::path ResolvedMtlPath = ResolveMaterialReferencePath(ObjPath, ObjFileStringToUtf8(MtlFileName));
			ParseMtlFile(FPaths::FromPath(ResolvedMtlPath).c_str());
		}
		else if (Type == "usemtl")
		{
			Context.ParseUseMtl(SS);
		}
		else if (Type == "f")
		{
			Context.ParseFace(SS);
		}
		else if (Type == "v")
		{
			FVector Position;
			SS >> Position.X >> Position.Y >> Position.Z;
			Context.TempPositions.push_back(ConvertObjVectorToEngineBasis(Position, LoadOptions));
		}
		else if (Type == "vt")
		{
			FVector2 UV;
			SS >> UV.X >> UV.Y;
			UV.Y = 1.0f - UV.Y;
			Context.TempUVs.push_back(UV);
		}
		else if (Type == "vn")
		{
			FVector Normal;
			SS >> Normal.X >> Normal.Y >> Normal.Z;
			Context.TempNormals.push_back(ConvertObjVectorToEngineBasis(Normal, LoadOptions));
		}
	}

	Context.CloseCurrentSection();
	OutMesh->Topology = EMeshTopology::EMT_TriangleList;

	UE_LOG(
		"[FObjManager] Parsed OBJ: %s (Verts: %zu, Indices: %zu)",
		AbsolutePath.c_str(),
		OutMesh->Vertices.size(),
		OutMesh->Indices.size());

	return true;
}

void FObjManager::InvalidateCacheEntriesForAsset(const FString& PathFileName)
{
	const FString StandardizedPath = GetStandardizedMeshPath(PathFileName);
	const FString Extension        = GetNormalizedExtension(StandardizedPath);
	const FString ObjPath          = (Extension == ".model")
		                                 ? GetStandardizedMeshPath(GetObjFilePathFromModelPath(StandardizedPath))
		                                 : StandardizedPath;
	const FString ModelPath      = GetStandardizedMeshPath(GetModelFilePath(ObjPath));
	const FString ObjCachePrefix = ObjPath + "|OBJ|";

	auto EraseAndDelete = [](std::unordered_map<FString, UStaticMesh*>& Map, std::unordered_map<FString, UStaticMesh*>::iterator It)
	{
		if (It->second)
		{
			delete It->second;
		}
		return Map.erase(It);
	};

	{
		auto It = ObjStaticMeshMap.find(ModelPath);
		if (It != ObjStaticMeshMap.end())
		{
			EraseAndDelete(ObjStaticMeshMap, It);
		}
	}

	for (auto It = ObjStaticMeshMap.begin(); It != ObjStaticMeshMap.end();)
	{
		if (It->first == ObjPath || It->first.rfind(ObjCachePrefix, 0) == 0)
		{
			It = EraseAndDelete(ObjStaticMeshMap, It);
			continue;
		}

		++It;
	}
}
