#include "StatWindow.h"
#include "Object/Object.h"
#include "Object/Class.h"
#include "Object/ObjectGlobals.h"
#include "Memory/MemoryBase.h"
#include "Viewport/ViewportTypes.h"
#include "Renderer/Renderer.h"
#include "Renderer/Features/Decal/DecalRenderFeature.h"
#include "Renderer/Features/Fog/FogStats.h"
#include "Renderer/Features/Lighting/LightStats.h"
#include "Renderer/Features/Shadow/ShadowRenderFeature.h"
#include "Renderer/GPUStats.h"

#include "imgui.h"

#include <cstdio>
#include <string>
#include <vector>

namespace
{
	struct FStatLine
	{
		std::string Label;
		std::string Value;
		bool bHeader = false;
		bool bSeparatorBefore = false;
		bool bSeparatorAfter = false;
	};

	float ClampFloat(float Value, float MinValue, float MaxValue)
	{
		if (Value < MinValue)
		{
			return MinValue;
		}
		if (Value > MaxValue)
		{
			return MaxValue;
		}
		return Value;
	}

	std::string FitTextToWidth(const std::string& Text, float MaxWidth)
	{
		if (MaxWidth <= 8.0f)
		{
			return "";
		}

		if (ImGui::CalcTextSize(Text.c_str()).x <= MaxWidth)
		{
			return Text;
		}

		static const char* Ellipsis = "...";
		const float EllipsisWidth = ImGui::CalcTextSize(Ellipsis).x;

		if (EllipsisWidth > MaxWidth)
		{
			return "";
		}

		std::string Result = Text;
		while (!Result.empty())
		{
			Result.pop_back();

			std::string Candidate = Result + Ellipsis;
			if (ImGui::CalcTextSize(Candidate.c_str()).x <= MaxWidth)
			{
				return Candidate;
			}
		}

		return Ellipsis;
	}

	void AddStatLine(std::vector<FStatLine>& Lines, const std::string& Label, const std::string& Value)
	{
		Lines.push_back({ Label, Value, false, false, false });
	}

	void AddStatHeader(
		std::vector<FStatLine>& Lines,
		const std::string& Header,
		bool bSeparatorBefore = true,
		bool bSeparatorAfter = true)
	{
		Lines.push_back({ Header, "", true, bSeparatorBefore, bSeparatorAfter });
	}

	void RenderStatTable(const char* TableId, const std::vector<FStatLine>& Lines)
	{
		const ImGuiTableFlags TableFlags =
			ImGuiTableFlags_SizingStretchProp |
			ImGuiTableFlags_BordersInnerV |
			ImGuiTableFlags_PadOuterX;

		if (ImGui::BeginTable(TableId, 2, TableFlags))
		{
			ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthStretch, 0.68f);
			ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 0.32f);

			for (const FStatLine& Line : Lines)
			{
				if (Line.bSeparatorBefore)
				{
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::Separator();
					ImGui::TableSetColumnIndex(1);
					ImGui::Separator();
				}

				ImGui::TableNextRow();
				if (Line.bHeader)
				{
					ImGui::TableSetColumnIndex(0);
					ImGui::Text("%s", Line.Label.c_str());
					ImGui::TableSetColumnIndex(1);
					ImGui::TextUnformatted("");

					if (Line.bSeparatorAfter)
					{
						ImGui::TableNextRow();
						ImGui::TableSetColumnIndex(0);
						ImGui::Separator();
						ImGui::TableSetColumnIndex(1);
						ImGui::Separator();
					}

					continue;
				}

				ImGui::TableSetColumnIndex(0);
				ImGui::TextUnformatted(Line.Label.c_str());
				ImGui::TableSetColumnIndex(1);
				ImGui::Text("%s", Line.Value.c_str());
			}

			ImGui::EndTable();
		}
	}

	void RenderStatTablePanel(const char* ChildId, const std::vector<FStatLine>& Lines)
	{
		ImGui::BeginChild(
			ChildId,
			ImVec2(0.0f, 0.0f),
			false,
			ImGuiWindowFlags_AlwaysVerticalScrollbar
		);

		RenderStatTable("StatsTable", Lines);

		ImGui::EndChild();
	}

	void RenderDirectionalShadowAtlasPreview(FShadowRenderFeature* ShadowFeature)
	{
		if (!ShadowFeature)
		{
			return;
		}

		ID3D11ShaderResourceView* AtlasSRV = ShadowFeature->GetDirShadowDepthAtlasSRV();
		if (!AtlasSRV)
		{
			ImGui::TextDisabled("Directional shadow atlas unavailable.");
			return;
		}

		const float AvailableWidth = ImGui::GetContentRegionAvail().x;
		const float PreviewSize = ClampFloat(AvailableWidth, 96.0f, 220.0f);
		const float AtlasSize = static_cast<float>(ShadowConfig::DirMaxShadowDepthResolution);

		ImGui::TextDisabled("Directional Shadow Atlas");
		const ImVec2 Cursor = ImGui::GetCursorScreenPos();
		ImGui::GetWindowDrawList()->AddRectFilled(
			Cursor,
			ImVec2(Cursor.x + PreviewSize, Cursor.y + PreviewSize),
			IM_COL32(18, 18, 18, 255));
		ImGui::Image(
			reinterpret_cast<ImTextureID>(AtlasSRV),
			ImVec2(PreviewSize, PreviewSize),
			ImVec2(0.0f, 0.0f),
			ImVec2(1.0f, 1.0f));

		const ImVec2 ImageMin = ImGui::GetItemRectMin();
		ImDrawList* DrawList = ImGui::GetWindowDrawList();
		const TArray<FShadowViewRenderItem>& Cascades = ShadowFeature->GetLastDirectionalShadowViews();
		const ImU32 Colors[] = {
			IM_COL32(255, 210, 64, 255),
			IM_COL32(96, 220, 255, 255),
			IM_COL32(140, 255, 128, 255),
			IM_COL32(255, 128, 220, 255),
		};

		for (uint32 Index = 0; Index < static_cast<uint32>(Cascades.size()); ++Index)
		{
			const FShadowViewRenderItem& Cascade = Cascades[Index];
			if (!Cascade.bAtlasAllocated || Cascade.AtlasUV.Z <= 0.0f)
			{
				continue;
			}

			const float X0 = ImageMin.x + (Cascade.AtlasUV.X / AtlasSize) * PreviewSize;
			const float Y0 = ImageMin.y + (Cascade.AtlasUV.Y / AtlasSize) * PreviewSize;
			const float X1 = ImageMin.x + ((Cascade.AtlasUV.X + Cascade.AtlasUV.Z) / AtlasSize) * PreviewSize;
			const float Y1 = ImageMin.y + ((Cascade.AtlasUV.Y + Cascade.AtlasUV.Z) / AtlasSize) * PreviewSize;
			const ImU32 Color = Colors[Index % 4];

			DrawList->AddRectFilled(ImVec2(X0, Y0), ImVec2(X1, Y1), Color & IM_COL32(255, 255, 255, 48));
			DrawList->AddRect(ImVec2(X0, Y0), ImVec2(X1, Y1), IM_COL32(0, 0, 0, 255), 0.0f, 0, 4.0f);
			DrawList->AddRect(ImVec2(X0, Y0), ImVec2(X1, Y1), Color, 0.0f, 0, 2.5f);

			char Label[16];
			std::snprintf(Label, sizeof(Label), "%u", Index);
			const ImVec2 TextPos(X0 + 6.0f, Y0 + 5.0f);
			const ImVec2 TextSize = ImGui::CalcTextSize(Label);
			DrawList->AddRectFilled(
				ImVec2(TextPos.x - 3.0f, TextPos.y - 2.0f),
				ImVec2(TextPos.x + TextSize.x + 3.0f, TextPos.y + TextSize.y + 2.0f),
				IM_COL32(0, 0, 0, 190));
			DrawList->AddText(TextPos, Color, Label);
		}

		ImGui::Spacing();
	}

	void RenderSpotShadowAtlasPreview(FShadowRenderFeature* ShadowFeature, const AActor* SelectedActor)
	{
		if (!ShadowFeature)
		{
			return;
		}

		ID3D11ShaderResourceView* AtlasSRV = ShadowFeature->GetLocalShadowAtlasPreviewSRV();
		if (!AtlasSRV)
		{
			ImGui::TextDisabled("Spot shadow atlas unavailable.");
			return;
		}

		const float AvailableWidth = ImGui::GetContentRegionAvail().x;
		const float PreviewSize = ClampFloat(AvailableWidth, 96.0f, 220.0f);
		const float AtlasSize = static_cast<float>(ShadowConfig::MaxShadowMapResolution);

		ImGui::TextDisabled("Spot Shadow Atlas");
		const ImVec2 Cursor = ImGui::GetCursorScreenPos();
		ImGui::GetWindowDrawList()->AddRectFilled(
			Cursor,
			ImVec2(Cursor.x + PreviewSize, Cursor.y + PreviewSize),
			IM_COL32(18, 18, 18, 255));
		ImGui::Image(
			reinterpret_cast<ImTextureID>(AtlasSRV),
			ImVec2(PreviewSize, PreviewSize),
			ImVec2(0.0f, 0.0f),
			ImVec2(1.0f, 1.0f));

		const ImVec2 ImageMin = ImGui::GetItemRectMin();
		ImDrawList* DrawList = ImGui::GetWindowDrawList();
		const TArray<FShadowViewRenderItem>& Views = ShadowFeature->GetLastLocalShadowViews();
		const ImU32 Colors[] = {
			IM_COL32(255, 210, 64, 255),
			IM_COL32(96, 220, 255, 255),
			IM_COL32(140, 255, 128, 255),
			IM_COL32(255, 128, 220, 255),
			IM_COL32(255, 150, 96, 255),
			IM_COL32(180, 160, 255, 255),
		};

		bool bDrewSelectedSpot = false;
		for (uint32 ViewIndex = 0; ViewIndex < static_cast<uint32>(Views.size()); ++ViewIndex)
		{
			const FShadowViewRenderItem& View = Views[ViewIndex];
			if (View.LightType != EShadowLightType::Spot || !View.bAtlasAllocated || View.AtlasUV.Z <= 0.0f)
			{
				continue;
			}
			if (!SelectedActor || View.SourceActor != SelectedActor)
			{
				continue;
			}

			const float X0 = ImageMin.x + (View.AtlasUV.X / AtlasSize) * PreviewSize;
			const float Y0 = ImageMin.y + (View.AtlasUV.Y / AtlasSize) * PreviewSize;
			const float X1 = ImageMin.x + ((View.AtlasUV.X + View.AtlasUV.Z) / AtlasSize) * PreviewSize;
			const float Y1 = ImageMin.y + ((View.AtlasUV.Y + View.AtlasUV.Z) / AtlasSize) * PreviewSize;
			const ImU32 Color = Colors[ViewIndex % 6];

			DrawList->AddRectFilled(ImVec2(X0, Y0), ImVec2(X1, Y1), Color & IM_COL32(255, 255, 255, 48));
			DrawList->AddRect(ImVec2(X0, Y0), ImVec2(X1, Y1), IM_COL32(0, 0, 0, 255), 0.0f, 0, 4.0f);
			DrawList->AddRect(ImVec2(X0, Y0), ImVec2(X1, Y1), Color, 0.0f, 0, 2.5f);
			bDrewSelectedSpot = true;
		}

		if (!bDrewSelectedSpot)
		{
			ImGui::TextDisabled("No selected spot shadow view.");
		}

		ImGui::Spacing();
	}

	void RenderShadowAtlasPreviewRow(FShadowRenderFeature* ShadowFeature, const AActor* SelectedActor)
	{
		const ImGuiTableFlags TableFlags =
			ImGuiTableFlags_SizingStretchSame |
			ImGuiTableFlags_PadOuterX;

		if (ImGui::BeginTable("ShadowAtlasPreviewRow", 2, TableFlags))
		{
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			RenderDirectionalShadowAtlasPreview(ShadowFeature);

			ImGui::TableSetColumnIndex(1);
			RenderSpotShadowAtlasPreview(ShadowFeature, SelectedActor);

			ImGui::EndTable();
		}
		else
		{
			RenderDirectionalShadowAtlasPreview(ShadowFeature);
			RenderSpotShadowAtlasPreview(ShadowFeature, SelectedActor);
		}
	}
}

void FStatWindow::RefreshObjectList()
{
	ObjectEntries.clear();

	for (UObject* Obj : GUObjectArray)
	{
		if (Obj == nullptr)
		{
			continue;
		}

		FObjectEntry Entry;
		Entry.Name = Obj->GetName();
		Entry.ClassName = Obj->GetClass() ? Obj->GetClass()->GetName() : "Unknown";
		Entry.Size = Obj->ObjectSize;
		ObjectEntries.push_back(Entry);
	}

	bShowObjectList = true;
}

void FStatWindow::Render(const FRect& AreaRect, EStatWindowMode Mode, FRenderer* Renderer, AActor* SelectedActor)
{
	if (Mode == EStatWindowMode::Memory)
	{
		RefreshObjectList();
	}

	const float ViewportWidth = static_cast<float>(AreaRect.Width);
	const float ViewportHeight = static_cast<float>(AreaRect.Height);
	const float MarginX = 12.0f;
	const float MarginY = 12.0f;

	const float MaxWindowWidth = (ViewportWidth > MarginX * 2.0f) ? (ViewportWidth - MarginX * 2.0f) : ViewportWidth;
	const float MaxWindowHeight = (ViewportHeight > MarginY * 2.0f) ? (ViewportHeight - MarginY * 2.0f) : ViewportHeight;

	float WindowWidth = ViewportWidth * 0.50f;
	float WindowHeight = ViewportHeight * 0.50f;

	WindowWidth = ClampFloat(WindowWidth, 280.0f, MaxWindowWidth);
	WindowHeight = ClampFloat(WindowHeight, 180.0f, MaxWindowHeight);

	ImGuiViewport* MainVp = ImGui::GetMainViewport();
	const float WindowX = MainVp->Pos.x + AreaRect.X + MarginX;
	const float WindowY = MainVp->Pos.y + AreaRect.Y + MarginY;

	ImGui::SetNextWindowPos(ImVec2(WindowX, WindowY), ImGuiCond_Always);
	ImGui::SetNextWindowSize(ImVec2(WindowWidth, WindowHeight), ImGuiCond_Always);
	ImGui::SetNextWindowBgAlpha(0.35f);

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f, 12.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 6.0f));

	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.02f, 0.02f, 0.02f, 0.72f));
	ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1.00f, 1.00f, 1.00f, 0.08f));
	ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(1.00f, 1.00f, 1.00f, 0.12f));
	ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.95f, 0.95f, 1.00f));
	ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(1.00f, 1.00f, 1.00f, 0.04f));

	ImGuiWindowFlags Flags =
		ImGuiWindowFlags_NoDecoration |
		ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoSavedSettings |
		ImGuiWindowFlags_NoDocking;

	const bool bOpen = ImGui::Begin("##StatsOverlay", nullptr, Flags);

	ImGui::PopStyleColor(5);
	ImGui::PopStyleVar(4);

	if (!bOpen)
	{
		ImGui::End();
		return;
	}

	ImGui::Text("Statistics");
	ImGui::Separator();

	switch (Mode)
	{
	case EStatWindowMode::Memory:
		RenderMemoryStats();
		break;
	case EStatWindowMode::Decal:
		RenderDecalStats(Renderer);
		break;
	case EStatWindowMode::Fog:
		RenderFogStats(Renderer);
		break;
	case EStatWindowMode::GPU:
		RenderGPUStats(Renderer);
		break;
	case EStatWindowMode::Light:
		RenderLightStats(Renderer, SelectedActor);
		break;
	default:
		break;
	}

	ImGui::End();
}

void FStatWindow::RenderMemoryStats()
{
	ImGui::Text("Objects : %u", ObjectEntries.size());
	ImGui::Text("Current Heap Usage : %.2f KB", GetGMalloc()->MallocStats.CurrentAllocationBytes / 1024.0f);
	ImGui::Text("Current Heap Count : %d", GetGMalloc()->MallocStats.CurrentAllocationCount);

	if (!bShowObjectList || ObjectEntries.empty())
	{
		return;
	}

	ImGui::Spacing();
	ImGui::Text("Object List");
	ImGui::Separator();

	ImGui::BeginChild(
		"ObjectListPanel",
		ImVec2(0.0f, 0.0f),
		false,
		ImGuiWindowFlags_AlwaysVerticalScrollbar
	);

	const float StartX = ImGui::GetCursorPosX();
	const float PanelWidth = ImGui::GetContentRegionAvail().x;

	float SizeColumnWidth = (PanelWidth < 460.0f) ? 72.0f : 96.0f;
	float Gap1 = 14.0f;
	float Gap2 = 14.0f;

	float ClassColumnWidth = (PanelWidth < 560.0f) ? (PanelWidth * 0.34f) : (PanelWidth * 0.30f);
	if (ClassColumnWidth < 80.0f)
	{
		ClassColumnWidth = 80.0f;
	}

	float NameColumnWidth = PanelWidth - ClassColumnWidth - SizeColumnWidth - Gap1 - Gap2;
	if (NameColumnWidth < 120.0f)
	{
		NameColumnWidth = 120.0f;
		ClassColumnWidth = PanelWidth - NameColumnWidth - SizeColumnWidth - Gap1 - Gap2;
		if (ClassColumnWidth < 70.0f)
		{
			ClassColumnWidth = 70.0f;
		}
	}

	const float NameX = StartX + ClassColumnWidth + Gap1;
	const float SizeX = NameX + NameColumnWidth + Gap2;

	ImGui::TextDisabled("Class");
	ImGui::SameLine(NameX);
	ImGui::TextDisabled("Name");
	ImGui::SameLine(SizeX);
	ImGui::TextDisabled("Size (Bytes)");
	ImGui::Separator();

	ImGuiListClipper Clipper;
	Clipper.Begin(static_cast<int>(ObjectEntries.size()));

	while (Clipper.Step())
	{
		for (int Index = Clipper.DisplayStart; Index < Clipper.DisplayEnd; ++Index)
		{
			const FObjectEntry& Entry = ObjectEntries[Index];

			const std::string ClassText = FitTextToWidth(Entry.ClassName, ClassColumnWidth);
			const std::string NameText = FitTextToWidth(Entry.Name, NameColumnWidth);

			char SizeBuffer[32];
			std::snprintf(SizeBuffer, sizeof(SizeBuffer), "%u", Entry.Size);

			const float SizeTextWidth = ImGui::CalcTextSize(SizeBuffer).x;
			float SizeTextX = SizeX + (SizeColumnWidth - SizeTextWidth);
			if (SizeTextX < SizeX)
			{
				SizeTextX = SizeX;
			}

			ImGui::TextDisabled("%s", ClassText.c_str());
			ImGui::SameLine(NameX);
			ImGui::Text("%s", NameText.c_str());
			ImGui::SameLine(SizeTextX);
			ImGui::Text("%s", SizeBuffer);
		}
	}

	ImGui::EndChild();
}

void FStatWindow::RenderDecalStats(FRenderer* Renderer)
{
	if (!Renderer)
	{
		ImGui::TextDisabled("Renderer unavailable.");
		return;
	}

	const FDecalStats Stats = Renderer->GetDecalStats();
	std::vector<FStatLine> Lines;

	const char* ModeString = "Unknown";
	switch (Stats.Common.Mode)
	{
	case EDecalProjectionMode::VolumeDraw:
		ModeString = "Volume Draw";
		break;
	case EDecalProjectionMode::ClusteredLookup:
		ModeString = "Clustered Lookup";
		break;
	default:
		break;
	}

	char Buffer[64];
	AddStatHeader(Lines, "[Decal Stat]", false);
	AddStatLine(Lines, "Mode", ModeString);
	std::snprintf(Buffer, sizeof(Buffer), "%.3f ms", Stats.Common.BuildTimeMs);
	AddStatLine(Lines, "Build Time", Buffer);
	std::snprintf(Buffer, sizeof(Buffer), "%.3f ms", Stats.Common.CullIntersectionTimeMs);
	AddStatLine(Lines, "Cull / Intersection Time", Buffer);
	std::snprintf(Buffer, sizeof(Buffer), "%.3f ms", Stats.Common.ShadingPassTimeMs);
	AddStatLine(Lines, "Shading Pass Time", Buffer);
	std::snprintf(Buffer, sizeof(Buffer), "%.3f ms", Stats.Common.TotalDecalTimeMs);
	AddStatLine(Lines, "Total Decal Time", Buffer);

	if (Stats.Common.Mode == EDecalProjectionMode::VolumeDraw)
	{
		AddStatHeader(Lines, "[Volume Draw]");
		std::snprintf(Buffer, sizeof(Buffer), "%u", Stats.Volume.CandidateObjects);
		AddStatLine(Lines, "Candidate Objects", Buffer);
		std::snprintf(Buffer, sizeof(Buffer), "%u", Stats.Volume.IntersectPassed);
		AddStatLine(Lines, "Intersect Passed", Buffer);
		std::snprintf(Buffer, sizeof(Buffer), "%u", Stats.Volume.DecalDrawCalls);
		AddStatLine(Lines, "Decal Draw Calls", Buffer);
		const double IntersectPassRatio = Stats.Volume.CandidateObjects > 0
			? (static_cast<double>(Stats.Volume.IntersectPassed) / static_cast<double>(Stats.Volume.CandidateObjects)) * 100.0
			: 0.0;
		std::snprintf(Buffer, sizeof(Buffer), "%.1f%%", IntersectPassRatio);
		AddStatLine(Lines, "Intersection Pass Ratio", Buffer);
	}
	else if (Stats.Common.Mode == EDecalProjectionMode::ClusteredLookup)
	{
		AddStatHeader(Lines, "[Clustered Lookup]");
		std::snprintf(Buffer, sizeof(Buffer), "%u", Stats.ClusteredLookup.ClustersBuilt);
		AddStatLine(Lines, "Clusters Built", Buffer);
		std::snprintf(Buffer, sizeof(Buffer), "%u", Stats.ClusteredLookup.NonEmptyClusters);
		AddStatLine(Lines, "Non-Empty Clusters", Buffer);
		std::snprintf(Buffer, sizeof(Buffer), "%u", Stats.ClusteredLookup.DecalCellRegistrations);
		AddStatLine(Lines, "Decal-Cell Registrations", Buffer);
		std::snprintf(Buffer, sizeof(Buffer), "%.3f", Stats.ClusteredLookup.AvgDecalsPerCell);
		AddStatLine(Lines, "Avg Decals Per Cell", Buffer);
		std::snprintf(Buffer, sizeof(Buffer), "%.3f", Stats.ClusteredLookup.AvgDecalsPerNonEmptyCell);
		AddStatLine(Lines, "Avg Decals Per Non-Empty Cell", Buffer);
		std::snprintf(Buffer, sizeof(Buffer), "%.3f", Stats.ClusteredLookup.AvgCellRegistrationsPerVisibleDecal);
		AddStatLine(Lines, "Avg Cell Registrations Per Visible Decal", Buffer);
		std::snprintf(Buffer, sizeof(Buffer), "%u", Stats.ClusteredLookup.MaxDecalsPerCell);
		AddStatLine(Lines, "Max Decals Per Cell", Buffer);
		AddStatHeader(Lines, "[Clustered Upload]");
		std::snprintf(Buffer, sizeof(Buffer), "%u", Stats.ClusteredLookup.UploadedDecalCount);
		AddStatLine(Lines, "Uploaded Decal Entries", Buffer);
		std::snprintf(Buffer, sizeof(Buffer), "%u", Stats.ClusteredLookup.UploadedClusterHeaderCount);
		AddStatLine(Lines, "Uploaded Cluster Headers", Buffer);
		std::snprintf(Buffer, sizeof(Buffer), "%u", Stats.ClusteredLookup.UploadedClusterIndexCount);
		AddStatLine(Lines, "Uploaded Cluster Indices", Buffer);
		std::snprintf(Buffer, sizeof(Buffer), "%.2f KB", Stats.ClusteredLookup.DecalBufferBytes / 1024.0);
		AddStatLine(Lines, "Decal Buffer", Buffer);
		std::snprintf(Buffer, sizeof(Buffer), "%.2f KB", Stats.ClusteredLookup.ClusterHeaderBufferBytes / 1024.0);
		AddStatLine(Lines, "Cluster Header Buffer", Buffer);
		std::snprintf(Buffer, sizeof(Buffer), "%.2f KB", Stats.ClusteredLookup.ClusterIndexBufferBytes / 1024.0);
		AddStatLine(Lines, "Cluster Index Buffer", Buffer);
		std::snprintf(Buffer, sizeof(Buffer), "%.2f KB", Stats.ClusteredLookup.TotalUploadBytes / 1024.0);
		AddStatLine(Lines, "Total Upload", Buffer);
	}

	RenderStatTablePanel("DecalStatPanel", Lines);
}

void FStatWindow::RenderFogStats(FRenderer* Renderer)
{
	if (!Renderer)
	{
		ImGui::TextDisabled("Renderer unavailable.");
		return;
	}

	const FFogStats Stats = Renderer->GetFogStats();
	std::vector<FStatLine> Lines;
	char Buffer[64];

	AddStatHeader(Lines, "[Fog Stat]", false);
	std::snprintf(Buffer, sizeof(Buffer), "%u", Stats.Common.TotalFogVolumes);
	AddStatLine(Lines, "Total Fog Volumes", Buffer);
	std::snprintf(Buffer, sizeof(Buffer), "%u", Stats.Common.GlobalFogVolumes);
	AddStatLine(Lines, "Global Fog Volumes", Buffer);
	std::snprintf(Buffer, sizeof(Buffer), "%u", Stats.Common.LocalFogVolumes);
	AddStatLine(Lines, "Local Fog Volumes", Buffer);
	std::snprintf(Buffer, sizeof(Buffer), "%u", Stats.Common.RegisteredLocalFogVolumes);
	AddStatLine(Lines, "Registered Local Fog Volumes", Buffer);
	std::snprintf(Buffer, sizeof(Buffer), "%u", Stats.Common.ClusterCount);
	AddStatLine(Lines, "Cluster Count", Buffer);
	std::snprintf(Buffer, sizeof(Buffer), "%u", Stats.Common.NonEmptyClusterCount);
	AddStatLine(Lines, "Non-Empty Clusters", Buffer);
	std::snprintf(Buffer, sizeof(Buffer), "%u", Stats.Common.ClusterIndexCount);
	AddStatLine(Lines, "Cluster Index Count", Buffer);
	std::snprintf(Buffer, sizeof(Buffer), "%u", Stats.Common.MaxFogPerCluster);
	AddStatLine(Lines, "Max Fog Per Cluster", Buffer);
	std::snprintf(Buffer, sizeof(Buffer), "%u", Stats.Common.FullscreenPassCount);
	AddStatLine(Lines, "Fullscreen Pass Count", Buffer);
	std::snprintf(Buffer, sizeof(Buffer), "%u", Stats.Common.DrawCallCount);
	AddStatLine(Lines, "Draw Call Count", Buffer);
	std::snprintf(Buffer, sizeof(Buffer), "%.3f ms", Stats.Common.ClusterBuildTimeMs);
	AddStatLine(Lines, "Cluster Build Time", Buffer);
	std::snprintf(Buffer, sizeof(Buffer), "%.3f ms", Stats.Common.ConstantBufferUpdateTimeMs);
	AddStatLine(Lines, "Constant Buffer Update Time", Buffer);
	std::snprintf(Buffer, sizeof(Buffer), "%.3f ms", Stats.Common.StructuredBufferUploadTimeMs);
	AddStatLine(Lines, "Structured Buffer Upload Time", Buffer);
	std::snprintf(Buffer, sizeof(Buffer), "%.3f ms", Stats.Common.ShadingPassTimeMs);
	AddStatLine(Lines, "Shading Pass Time", Buffer);
	std::snprintf(Buffer, sizeof(Buffer), "%.3f ms", Stats.Common.TotalFogTimeMs);
	AddStatLine(Lines, "Total Fog Time", Buffer);
	std::snprintf(Buffer, sizeof(Buffer), "%.2f KB", Stats.Common.GlobalFogBufferBytes / 1024.0);
	AddStatLine(Lines, "Global Fog Buffer", Buffer);
	std::snprintf(Buffer, sizeof(Buffer), "%.2f KB", Stats.Common.LocalFogBufferBytes / 1024.0);
	AddStatLine(Lines, "Local Fog Buffer", Buffer);
	std::snprintf(Buffer, sizeof(Buffer), "%.2f KB", Stats.Common.ClusterHeaderBufferBytes / 1024.0);
	AddStatLine(Lines, "Cluster Header Buffer", Buffer);
	std::snprintf(Buffer, sizeof(Buffer), "%.2f KB", Stats.Common.ClusterIndexBufferBytes / 1024.0);
	AddStatLine(Lines, "Cluster Index Buffer", Buffer);
	std::snprintf(Buffer, sizeof(Buffer), "%.2f MB", Stats.Common.SceneColorCopyBytes / (1024.0 * 1024.0));
	AddStatLine(Lines, "SceneColor Copy", Buffer);
	std::snprintf(Buffer, sizeof(Buffer), "%.2f KB", Stats.Common.TotalUploadBytes / 1024.0);
	AddStatLine(Lines, "Total Upload", Buffer);

	RenderStatTablePanel("FogStatPanel", Lines);
}

void FStatWindow::RenderGPUStats(FRenderer* Renderer)
{
	if (!Renderer)
	{
		ImGui::TextDisabled("Renderer unavailable.");
		return;
	}

	const FGPUFrameStats Stats = Renderer->GetGPUStats();
	std::vector<FStatLine> Lines;
	char Buffer[64];

	AddStatHeader(Lines, "[GPU Stat]", false);
	std::snprintf(Buffer, sizeof(Buffer), "%u", Stats.GeometryDrawCalls);
	AddStatLine(Lines, "Geometry Draw Calls", Buffer);
	std::snprintf(Buffer, sizeof(Buffer), "%u", Stats.FullscreenPassCount);
	AddStatLine(Lines, "Fullscreen Pass Count", Buffer);
	std::snprintf(Buffer, sizeof(Buffer), "%u", Stats.DrawCallCount);
	AddStatLine(Lines, "Total Draw Calls", Buffer);
	std::snprintf(Buffer, sizeof(Buffer), "%u", Stats.PassCount);
	AddStatLine(Lines, "Pass Count", Buffer);
	std::snprintf(Buffer, sizeof(Buffer), "%.3f ms", Stats.GeometryTimeMs);
	AddStatLine(Lines, "Geometry Cost", Buffer);
	std::snprintf(Buffer, sizeof(Buffer), "%.3f ms", Stats.PixelShadingTimeMs);
	AddStatLine(Lines, "Pixel Shading Cost", Buffer);
	std::snprintf(Buffer, sizeof(Buffer), "%.3f ms", Stats.MemoryBandwidthTimeMs);
	AddStatLine(Lines, "Memory / Bandwidth Cost", Buffer);
	std::snprintf(Buffer, sizeof(Buffer), "%.3f ms", Stats.OverdrawFillrateTimeMs);
	AddStatLine(Lines, "Overdraw / Fillrate Cost", Buffer);
	std::snprintf(Buffer, sizeof(Buffer), "%u", Stats.DecalDrawCalls);
	AddStatLine(Lines, "Decal Draw Calls", Buffer);
	std::snprintf(Buffer, sizeof(Buffer), "%u", Stats.FogDrawCalls);
	AddStatLine(Lines, "Fog Draw Calls", Buffer);
	std::snprintf(Buffer, sizeof(Buffer), "%.2f KB", Stats.UploadBytes / 1024.0);
	AddStatLine(Lines, "Upload Bytes", Buffer);
	std::snprintf(Buffer, sizeof(Buffer), "%.2f MB", Stats.CopyBytes / (1024.0 * 1024.0));
	AddStatLine(Lines, "Scene Copy Bytes", Buffer);
	std::snprintf(Buffer, sizeof(Buffer), "%.2f M", Stats.EstimatedFullscreenPixels / 1000000.0);
	AddStatLine(Lines, "Estimated Fullscreen Pixels", Buffer);

	RenderStatTablePanel("GPUStatPanel", Lines);
	ImGui::Spacing();
	ImGui::TextDisabled("Geometry/overdraw are engine-side aggregates. D3D11 hardware counters are not sampled here.");
}

void FStatWindow::RenderLightStats(FRenderer* Renderer, AActor* SelectedActor)
{
	if (!Renderer)
	{
		ImGui::TextDisabled("Renderer unavailable.");
		return;
	}

	const FLightStats Stats = Renderer->GetLightStats();
	std::vector<FStatLine> Lines;
	char Buffer[64];

	AddStatHeader(Lines, "[Light Culling Stat]", false);
	std::snprintf(Buffer, sizeof(Buffer), "%u", Stats.TotalSceneLights);
	AddStatLine(Lines, "Scene Lights", Buffer);
	std::snprintf(Buffer, sizeof(Buffer), "%u / %u", Stats.TotalLocalLights, Stats.MaxLocalLights);
	AddStatLine(Lines, "Uploaded (Used / Max)", Buffer);
	std::snprintf(Buffer, sizeof(Buffer), "%u", Stats.BudgetCulledLights);
	AddStatLine(Lines, "Budget Culled (over cap)", Buffer);
	std::snprintf(Buffer, sizeof(Buffer), "%u", Stats.TotalLightClusterAssignments);
	AddStatLine(Lines, "Cluster-Light Pairs (prev frame)", Buffer);
	std::snprintf(Buffer, sizeof(Buffer), "%u", Stats.OverflowCulledSlots);
	AddStatLine(Lines, "Overflow Culled (per-cluster cap)", Buffer);
	std::snprintf(Buffer, sizeof(Buffer), "%u", Stats.ActiveClusters);
	AddStatLine(Lines, "Active Clusters (prev frame)", Buffer);
	std::snprintf(Buffer, sizeof(Buffer), "%.3f", Stats.AvgLightsPerActiveCluster);
	AddStatLine(Lines, "Avg Lights / Active Cluster", Buffer);

	AddStatHeader(Lines, "[Cluster Grid]");
	std::snprintf(Buffer, sizeof(Buffer), "%u x %u x %u", Stats.ClusterCountX, Stats.ClusterCountY, Stats.ClusterCountZ);
	AddStatLine(Lines, "Cluster Grid (X x Y x Z)", Buffer);
	std::snprintf(Buffer, sizeof(Buffer), "%u", Stats.TotalClusters);
	AddStatLine(Lines, "Total Clusters", Buffer);
	std::snprintf(Buffer, sizeof(Buffer), "%u", Stats.MaxLightsPerCluster);
	AddStatLine(Lines, "Max Lights Per Cluster", Buffer);

	AddStatHeader(Lines, "[GPU Time]");
	std::snprintf(Buffer, sizeof(Buffer), "%.3f ms", Stats.TileDepthBoundsPassTimeMs);
	AddStatLine(Lines, "Tile Depth Bounds Pass", Buffer);
	std::snprintf(Buffer, sizeof(Buffer), "%.3f ms", Stats.LightCullingPassTimeMs);
	AddStatLine(Lines, "Light Culling Pass", Buffer);
	std::snprintf(Buffer, sizeof(Buffer), "%.3f ms", Stats.TotalCullingTimeMs);
	AddStatLine(Lines, "Total Culling GPU Time", Buffer);

	AddStatHeader(Lines, "[GPU Buffers]");
	std::snprintf(Buffer, sizeof(Buffer), "%.2f KB", Stats.LocalLightBufferBytes / 1024.0);
	AddStatLine(Lines, "Local Light Buffer", Buffer);
	std::snprintf(Buffer, sizeof(Buffer), "%.2f KB", Stats.CullProxyBufferBytes / 1024.0);
	AddStatLine(Lines, "Cull Proxy Buffer", Buffer);
	std::snprintf(Buffer, sizeof(Buffer), "%.2f KB", Stats.TileDepthBoundsBufferBytes / 1024.0);
	AddStatLine(Lines, "Tile Depth Bounds Buffer", Buffer);
	std::snprintf(Buffer, sizeof(Buffer), "%.2f KB", Stats.ClusterHeaderBufferBytes / 1024.0);
	AddStatLine(Lines, "Cluster Header Buffer", Buffer);
	std::snprintf(Buffer, sizeof(Buffer), "%.2f KB", Stats.ClusterIndexBufferBytes / 1024.0);
	AddStatLine(Lines, "Cluster Index Buffer", Buffer);
	std::snprintf(Buffer, sizeof(Buffer), "%.2f KB", Stats.TotalBufferBytes / 1024.0);
	AddStatLine(Lines, "Total Buffer", Buffer);

	FShadowRenderFeature* ShadowFeature = Renderer->GetShadowFeature();
	AddStatHeader(Lines, "[Directional Shadow Cascades]");
	if (ShadowFeature)
	{
		const TArray<FShadowViewRenderItem>& Cascades = ShadowFeature->GetLastDirectionalShadowViews();
		if (Cascades.empty())
		{
			AddStatLine(Lines, "Cascade Count", "0");
		}
		else
		{
			std::snprintf(Buffer, sizeof(Buffer), "%u", static_cast<uint32>(Cascades.size()));
			AddStatLine(Lines, "Cascade Count", Buffer);

			for (uint32 Index = 0; Index < static_cast<uint32>(Cascades.size()); ++Index)
			{
				const FShadowViewRenderItem& Cascade = Cascades[Index];
				char Label[64];

				std::snprintf(Label, sizeof(Label), "Cascade %u", Index);
				AddStatLine(Lines, Label, Cascade.bAtlasAllocated ? "Allocated" : "Not allocated");

				std::snprintf(Label, sizeof(Label), "Cascade %u Rect", Index);
				std::snprintf(
					Buffer,
					sizeof(Buffer),
					"%.0f, %.0f, %.0f",
					Cascade.AtlasUV.X,
					Cascade.AtlasUV.Y,
					Cascade.AtlasUV.Z);
				AddStatLine(Lines, Label, Buffer);

				std::snprintf(Label, sizeof(Label), "Cascade %u Resolution", Index);
				std::snprintf(Buffer, sizeof(Buffer), "%u", Cascade.AllocatedResolution);
				AddStatLine(Lines, Label, Buffer);

				std::snprintf(Label, sizeof(Label), "Cascade %u Near/Far", Index);
				std::snprintf(Buffer, sizeof(Buffer), "%.2f / %.2f", Cascade.NearZ, Cascade.FarZ);
				AddStatLine(Lines, Label, Buffer);
			}
		}
	}
	else
	{
		AddStatLine(Lines, "Shadow Feature", "Unavailable");
	}

	ImGui::BeginChild(
		"LightStatPanel",
		ImVec2(0.0f, 0.0f),
		false,
		ImGuiWindowFlags_AlwaysVerticalScrollbar);

	RenderShadowAtlasPreviewRow(ShadowFeature, SelectedActor);
	RenderStatTable("LightStatsTable", Lines);

	ImGui::EndChild();
}
