# Renderer Refactor Notes

## Completed Structure

### Debug Path

- `FDebugDrawManager` now stops at `FDebugPrimitiveList`.
- Renderer-side helpers convert that neutral list into `FDebugLinePassInputs`.
- `FDebugLineRenderFeature` consumes prepared pass input instead of building request data or owning a feature-local line buffer path.

### UI / Frame Pass Path

- `FScreenUIRenderer` now has a two-step flow:
  - `BuildPassInputs(...)`
  - `Render(...)`
- `FFramePassContext` carries explicit `FViewportCompositePassInputs` and `FScreenUIPassInputs` instead of raw draw lists.
- Frame pipeline passes now consume prepared inputs only.

### Scene / Frame Pipeline

- Scene pipeline and frame pipeline now share the same `TPassPipeline` abstraction.
- This keeps separate contexts while forcing both pipelines to follow the same ordered-pass contract.
- A broader `IPassRegistry` or graph layer is not needed yet.

### Fullscreen Passes

- Fog, decal composite, outline composite/mask, and viewport composition now use `FFullscreenPassBindings` plus `ExecuteFullscreenPass(...)`.
- Shared rules are:
  - fullscreen triangle
  - explicit shader/CB/SRV/sampler binding table
  - centralized unbind/restore

### Scene View Data

- `FSceneViewData` is now split into:
  - `MeshInputs`
  - `PostProcessInputs`
  - `DebugInputs`
- New feature data should be added into one of those sub-structures instead of flattening the root struct again.

### Scene Command Extraction vs Cache

- `FSceneCommandBuilder` now focuses on extraction.
- Dynamic text/SubUV material caching moved behind `FSceneCommandResourceCache`.
- This keeps cache ownership separate from per-view packet traversal.

## Ownership Notes

### Renderer Permanent Owners

- `FRenderDevice`
- `FRenderStateManager`
- default materials and samplers
- feature services with persistent GPU state:
  - text
  - subUV
  - billboard
  - fog
  - outline
  - debug line
  - decal
- frame composition services:
  - `FViewportCompositor`
  - `FScreenUIRenderer`

### Target Ownership Split

- Scene core targets:
  - scene color
  - scene depth
- Supplemental targets:
  - GBuffer A/B/C
  - scene color scratch
  - outline mask

These are still owned by `FRenderer`, but they are now treated as two distinct ownership groups. A transient pool should replace the supplemental group only when post effects start competing for short-lived target reuse.

### Fullscreen Shader Ownership

- Feature-specific fullscreen shaders stay with their owning feature while the execution model is shared in `FullscreenPass.h`.
- A dedicated fullscreen shader registry is unnecessary until multiple features start hot-swapping the same shader families.

## Review Decisions

### Outline

- Outline mask remains a specialized mesh+stencil pass because it depends on per-item mesh/stencil policy.
- Outline composite is fully on the shared fullscreen path.
- Stencil policy remains feature-owned, but the execution boilerplate is no longer feature-specific.

### Decal

- `FDecalRenderFeature` remains a single feature boundary because clustering, GPU buffers, and composite draw all share frame-local ownership.
- Internal layering is now treated as:
  - prepare data
  - upload GPU resources
  - execute fullscreen composite
- If a later deferred-lighting pass reuses decal clustering, extract the prepare/upload stages into dedicated helpers first.

### Render Graph

- Current pass scheduling is still an ordered pass list by design.
- A render graph should be reconsidered only when at least one of these becomes true:
  - transient lifetime reuse becomes difficult to reason about
  - pass pruning becomes conditional and frequent
  - deferred lighting introduces real dependency fan-out

Until then, the current pass-sequence model is cheaper and clearer.
