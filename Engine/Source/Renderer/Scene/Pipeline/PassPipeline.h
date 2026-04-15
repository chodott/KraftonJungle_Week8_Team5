#pragma once

#include <memory>
#include <vector>

template <typename TPassInterface, typename TContext>
class TPassPipeline
{
public:
	void Reset()
	{
		Passes.clear();
	}

	void AddPass(std::unique_ptr<TPassInterface> Pass)
	{
		if (!Pass)
		{
			return;
		}

		Passes.push_back(std::move(Pass));
	}

	bool Execute(TContext& Context) const
	{
		for (const std::unique_ptr<TPassInterface>& Pass : Passes)
		{
			if (!Pass || !Pass->Execute(Context))
			{
				return false;
			}
		}

		return true;
	}

private:
	std::vector<std::unique_ptr<TPassInterface>> Passes;
};
