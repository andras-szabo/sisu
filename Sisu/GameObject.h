#pragma once
#include <cstddef>
#include <vector>
#include "Arena.h"
#include "SisuUtilities.h"

class GameObject
{
public:
	static std::size_t AddToArena(Arena<GameObject>& arena, GameObject go)
	{
		return arena.AddAnywhere(go);
	}

	static std::size_t AddChildren(Arena<GameObject>& arena, std::size_t parentIndex,
		std::vector<GameObject>::iterator begin,
		std::vector<GameObject>::iterator end)
	{
		std::size_t newChildrenCount = end - begin;
		if (!arena[parentIndex].hasChildren)
		{
			auto firstChildIndex = arena.GetStartIndexForGap(newChildrenCount, parentIndex);
			arena.AddAt(firstChildIndex, begin, end);

			arena[parentIndex].hasChildren = true;
			arena[parentIndex].childrenStartIndex = firstChildIndex;
			arena[parentIndex].childrenEndIndex = firstChildIndex + newChildrenCount - 1;
			
			for (auto i = 0; i < newChildrenCount; ++i)
			{
				arena[firstChildIndex + i].SetParent(parentIndex);
			}

			return firstChildIndex;
		}

		// Check the slot after the first kids; if OK, put them there.
		if (arena.CanAddItemsAt(arena[parentIndex].childrenEndIndex + 1, newChildrenCount))
		{
			auto firstChildIndex = arena[parentIndex].childrenEndIndex + 1;
			arena.AddAt(firstChildIndex, begin, end);

			arena[parentIndex].hasChildren = true;
			arena[parentIndex].childrenEndIndex = firstChildIndex + newChildrenCount - 1;

			for (auto i = 0; i < newChildrenCount; ++i)
			{
				arena[firstChildIndex + i].SetParent(parentIndex);
			}

			return arena[parentIndex].childrenStartIndex;
		}

		// So now we need to relocate.
		// TODO: DRY
		auto& parent = arena[parentIndex];
		auto existingKidCount = parent.childrenEndIndex - parent.childrenStartIndex + 1;
		auto childrenCount = existingKidCount + newChildrenCount;
		auto gapStartIndex = arena.GetStartIndexForGap(childrenCount, parentIndex);

		// First copy the existing kids and update their indices
		auto fromIndex = parent.childrenStartIndex;
		for (std::size_t i = 0; i < existingKidCount; ++i)
		{
			auto newIndex = gapStartIndex + i;
			arena.AddAt(newIndex, arena[fromIndex + i]);
			if (arena[newIndex].hasChildren)
			{
				for (std::size_t j = arena[newIndex].childrenStartIndex; j <= arena[newIndex].childrenEndIndex; ++j)
				{
					arena[j].parentIndex = newIndex;
				}
			}
		}

		arena.RemoveAt(fromIndex, existingKidCount);

		// Then add the new kids
		auto firstChildIndex = gapStartIndex + existingKidCount;
		arena.AddAt(firstChildIndex, begin, end);	
		
		for (auto i = 0; i < newChildrenCount; ++i)
		{
			arena[firstChildIndex + i].SetParent(parentIndex);
		}
	
		arena[parentIndex].childrenStartIndex = gapStartIndex;
		arena[parentIndex].childrenEndIndex = gapStartIndex + childrenCount - 1;

		return arena[parentIndex].childrenStartIndex;
	}

	static std::size_t AddChild(Arena<GameObject>& arena, std::size_t parentIndex, GameObject child)
	{
		if (!arena[parentIndex].hasChildren)
		{
			auto childIndex = arena.GetStartIndexForGap(1, parentIndex);
			arena.AddAt(childIndex, child);

			auto& parent = arena[parentIndex];

			parent.hasChildren = true;
			parent.childrenStartIndex = childIndex;
			parent.childrenEndIndex = childIndex;

			arena[childIndex].SetParent(parentIndex);

			return childIndex;
		}

		// Check the slot after last child of parent. If empty,
		// push the thing there.
		if (arena.CanAddItemAt(arena[parentIndex].childrenEndIndex + 1))
		{
			auto childIndex = arena[parentIndex].childrenEndIndex + 1;
			arena.AddAt(childIndex, child);
			arena[parentIndex].childrenEndIndex = childIndex;
			arena[childIndex].SetParent(parentIndex);
			return childIndex;
		}

		// We have to relocate all kids, including the new one
		auto& parent = arena[parentIndex];
		auto existingKidCount = parent.childrenEndIndex - parent.childrenStartIndex + 1;
		auto childrenCount = existingKidCount + 1;
		auto gapStartIndex = arena.GetStartIndexForGap(childrenCount, parentIndex);

		// First copy the existing kids and update their indices
		auto fromIndex = parent.childrenStartIndex;
		for (std::size_t i = 0; i < existingKidCount; ++i)
		{
			auto newIndex = gapStartIndex + i;
			arena.AddAt(newIndex, arena[fromIndex + i]);
			if (arena[newIndex].hasChildren)
			{
				for (std::size_t j = arena[newIndex].childrenStartIndex; j <= arena[newIndex].childrenEndIndex; ++j)
				{
					arena[j].parentIndex = newIndex;
				}
			}
		}

		arena.RemoveAt(fromIndex, existingKidCount);

		// Then add the new kid
		auto newKidsIndex = gapStartIndex + existingKidCount;
		arena.AddAt(newKidsIndex, child);
		arena[newKidsIndex].SetParent(parentIndex);

		// Then set up the parent
		arena[parentIndex].childrenStartIndex = gapStartIndex;
		arena[parentIndex].childrenEndIndex = newKidsIndex;

		return newKidsIndex;
	}

	//TODO: make sure we know when we're making copies
	GameObject()
	{
		localPosition = Sisu::Vector3::Zero();
		localRotation = Sisu::Vector3::Zero();
		localScale = Sisu::Vector3(1.0, 1.0, 1.0);
		color = Sisu::Color::Blue();
		transform = Sisu::Matrix4::Identity();
		velocityPerSec = Sisu::Vector3::Zero();
	}

	GameObject(const GameObject& other) = default;

	void SetParent(std::size_t parentIndex)
	{
		this->parentIndex = parentIndex;
		this->isRoot = false;
	}

	void RefreshTransform(Sisu::Matrix4* parentTransform);

public:
	//TODO: organize this nicely for alignment + only what's really needed
	Sisu::Matrix4 transform;
	Sisu::Quat rotQuat = Sisu::Quat::Identity();

	Sisu::Vector3 localPosition;
	Sisu::Vector3 localRotation;
	Sisu::Vector3 localScale;

	std::size_t childrenStartIndex = 0;
	std::size_t childrenEndIndex = 0;
	std::size_t parentIndex = 0;

	Sisu::Color color;
	Sisu::Color borderColor;

	bool isRoot = true;
	bool hasChildren = false;
	bool isVisible = true;

	Sisu::Vector3 velocityPerSec;
	Sisu::Vector3 eulerRotPerSec;
};