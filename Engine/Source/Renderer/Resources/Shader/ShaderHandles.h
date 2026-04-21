#pragma once

#include "Renderer/Resources/Shader/Shader.h"
#include "Renderer/Resources/Shader/ShaderCompiler.h"
#include "Renderer/Resources/Shader/ShaderRecipe.h"

#include <memory>
#include <mutex>
#include <shared_mutex>

class ENGINE_API IShaderHandle
{
public:
	virtual ~IShaderHandle() = default;

	virtual const FShaderRecipe& GetRecipe() const = 0;
	virtual FShaderRecipeKey GetKey() const = 0;
	virtual bool RebuildFromArtifact(ID3D11Device* Device, const FShaderCompileArtifact& Artifact, std::string& OutError) = 0;
};

class ENGINE_API FVertexShaderHandle final : public IShaderHandle
{
public:
	explicit FVertexShaderHandle(const FShaderRecipe& InRecipe);

	const FShaderRecipe& GetRecipe() const override { return Recipe; }
	FShaderRecipeKey GetKey() const override { return Key; }

	std::shared_ptr<FVertexShader> GetCurrent() const;
	void Bind(ID3D11DeviceContext* DeviceContext) const;

	bool RebuildFromArtifact(ID3D11Device* Device, const FShaderCompileArtifact& Artifact, std::string& OutError) override;

private:
	FShaderRecipe Recipe;
	FShaderRecipeKey Key;
	std::shared_ptr<FVertexShader> Current;
	mutable std::shared_mutex Mutex;
};

class ENGINE_API FPixelShaderHandle final : public IShaderHandle
{
public:
	explicit FPixelShaderHandle(const FShaderRecipe& InRecipe);

	const FShaderRecipe& GetRecipe() const override { return Recipe; }
	FShaderRecipeKey GetKey() const override { return Key; }

	std::shared_ptr<FPixelShader> GetCurrent() const;
	void Bind(ID3D11DeviceContext* DeviceContext) const;

	bool RebuildFromArtifact(ID3D11Device* Device, const FShaderCompileArtifact& Artifact, std::string& OutError) override;

private:
	FShaderRecipe Recipe;
	FShaderRecipeKey Key;
	std::shared_ptr<FPixelShader> Current;
	mutable std::shared_mutex Mutex;
};

class ENGINE_API FComputeShaderHandle final : public IShaderHandle
{
public:
	explicit FComputeShaderHandle(const FShaderRecipe& InRecipe);

	const FShaderRecipe& GetRecipe() const override { return Recipe; }
	FShaderRecipeKey GetKey() const override { return Key; }

	std::shared_ptr<FComputeShader> GetCurrent() const;
	void Bind(ID3D11DeviceContext* DeviceContext) const;

	bool RebuildFromArtifact(ID3D11Device* Device, const FShaderCompileArtifact& Artifact, std::string& OutError) override;

private:
	FShaderRecipe Recipe;
	FShaderRecipeKey Key;
	std::shared_ptr<FComputeShader> Current;
	mutable std::shared_mutex Mutex;
};
