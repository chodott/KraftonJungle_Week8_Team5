#pragma once
#include <memory>
#include <algorithm>

struct ShadowAtlasNode
{
	int X = 0;
	int Y = 0;
	int Size = 0;
	bool bUsed = false;
	std::unique_ptr<ShadowAtlasNode> Children[4];

	ShadowAtlasNode(int x, int y, int size)
		: X(x), Y(y), Size(size), bUsed(false)
	{
	}

	ShadowAtlasNode* Insert(int requiredSize);
	void Split();
};

class FShadowAtlasAllocator
{
public:
	FShadowAtlasAllocator(int atlasSize) : AtlasSize(atlasSize)
	{
		Reset();
	}

	void Reset()
	{
		RootNode = std::make_unique<ShadowAtlasNode>(0, 0, AtlasSize);
	}

	ShadowAtlasNode* Allocate(int requiredSize)
	{
		if (requiredSize <= 0) return nullptr;
		return RootNode->Insert(requiredSize);
	}

	int GetAtlasSize() const { return AtlasSize; }


private:
	int AtlasSize;
	std::unique_ptr<ShadowAtlasNode> RootNode;
};

