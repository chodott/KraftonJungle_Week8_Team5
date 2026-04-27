#include "ShadowAtlasAllocator.h"

ShadowAtlasNode* ShadowAtlasNode::Insert(int requiredSize)
{
	if (Children[0])
	{
		for (int i = 0; i < 4; ++i)
		{
			if (ShadowAtlasNode* InsertedNode = Children[i]->Insert(requiredSize))
			{
				return InsertedNode;
			}
		}
		return nullptr;
	}

	if (bUsed || Size < requiredSize)
	{
		return nullptr;
	}

	if (Size == requiredSize)
	{
		bUsed = true;
		return this;
	}

	Split();
    return Children[0]->Insert(requiredSize);
}

void ShadowAtlasNode::Split()
{
	int half = Size / 2;

	// 자식 노드 생성 (DirectX 좌표계: 우하단 방향으로 증가)
	Children[0] = std::make_unique<ShadowAtlasNode>(X, Y, half);                 // Top-Left
	Children[1] = std::make_unique<ShadowAtlasNode>(X + half, Y, half);          // Top-Right
	Children[2] = std::make_unique<ShadowAtlasNode>(X, Y + half, half);          // Bottom-Left
	Children[3] = std::make_unique<ShadowAtlasNode>(X + half, Y + half, half);   // Bottom-Right
}
