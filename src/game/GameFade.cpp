/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#include "game/GameFade.h"

#include <cmath>
#include <cstring>

#include <algorithm>
#include <format>
#include <utility>

#include <core/CameraLoop.h>
#include <spdlog/spdlog.h>

namespace moa_mod::camera
{

namespace
{
/// The layers of the objects that the fade looks for: the obstacles of the camera without the water and the UI.
constexpr int32_t k_FadeLayers = k_CameraObstacleLayers & ~((1 << 4) | (1 << 5));
/// `QueryTriggerInteraction.Ignore`: the tests do not find triggers.
constexpr int32_t k_IgnoreTriggers = 1;
/// An object with more renderers than this is not one building, tree, rock or cliff.
constexpr size_t k_MostRenderers = 256;
/// A renderer that cannot be see-through is hidden when the opacity of its object is below this value.
constexpr float k_HideWithoutFade = 0.75f;
// The values of `ShadowCastingMode`.
constexpr int32_t k_ShadowsOff = 0;
constexpr int32_t k_ShadowsOnly = 3;
// The transparent surface of the common shader: the blend of the alpha, with no depth write, in the transparent queue.
constexpr float k_SurfaceTransparent = 1.0f;
constexpr float k_BlendAlpha = 0.0f;
constexpr float k_BlendSourceAlpha = 5.0f;
constexpr float k_BlendOne = 1.0f;
constexpr float k_BlendOneMinusSourceAlpha = 10.0f;
constexpr int32_t k_TransparentQueue = 3000;
} // namespace

moa::Result<FadeBindings> FadeBindings::Bind(const Managed& managed)
{
	Binder binder(managed);
	FadeBindings b;
	auto* physics = binder.Class("UnityEngine", "Physics");
	auto* object = binder.Class("UnityEngine", "Object");
	auto* component = binder.Class("UnityEngine", "Component");
	auto* gameObject = binder.Class("UnityEngine", "GameObject");
	auto* renderer = binder.Class("UnityEngine", "Renderer");
	auto* meshRenderer = binder.Class("UnityEngine", "MeshRenderer");
	auto* lodGroup = binder.Class("UnityEngine", "LODGroup");
	auto* material = binder.Class("UnityEngine", "Material");
	auto* shader = binder.Class("UnityEngine", "Shader");
	auto* transform = binder.Class("UnityEngine", "Transform");
	auto* utilities = binder.Class("", "InputUtilities");
	auto* hand = binder.Class("", "PlayerHandManager");
	auto* legacy = binder.Class("", "NGLegacyBase");
	auto* moving = binder.Class("", "NGMovingObject");
	if (!binder.Errors().empty())
	{
		return moa::MakeError("{}", binder.Errors());
	}
	b.overlapSphere =
	    binder.Method(physics, "OverlapSphere",
	                  {"UnityEngine.Vector3", "System.Single", "System.Int32", "UnityEngine.QueryTriggerInteraction"},
	                  "UnityEngine.Collider[]", true);
	b.gameObject = binder.Method(component, "get_gameObject", {}, "UnityEngine.GameObject");
	b.componentTransform = binder.Method(component, "get_transform", {}, "UnityEngine.Transform");
	b.gameObjectTransform = binder.Method(gameObject, "get_transform", {}, "UnityEngine.Transform");
	b.isChildOf = binder.Method(transform, "IsChildOf", {"UnityEngine.Transform"}, "System.Boolean");
	b.currentDragObject = binder.Method(utilities, "GetCurrentDragObject", {}, "UnityEngine.GameObject", true);
	b.handInstance = binder.Field(hand, "m_handInstance", "UnityEngine.GameObject");
	b.componentInParent = binder.Method(component, "GetComponentInParent", {"System.Type"}, "UnityEngine.Component");
	b.componentsInChildren =
	    binder.Method(gameObject, "GetComponentsInChildren", {"System.Type", "System.Boolean"}, "UnityEngine.Component[]");
	b.sharedMaterials = binder.Method(renderer, "get_sharedMaterials", {}, "UnityEngine.Material[]");
	b.setSharedMaterials = binder.Method(renderer, "set_sharedMaterials", {"UnityEngine.Material[]"}, "System.Void");
	b.shadowMode = binder.Method(renderer, "get_shadowCastingMode", {}, "UnityEngine.Rendering.ShadowCastingMode");
	b.setShadowMode =
	    binder.Method(renderer, "set_shadowCastingMode", {"UnityEngine.Rendering.ShadowCastingMode"}, "System.Void");
	b.rendererEnabled = binder.Method(renderer, "get_enabled", {}, "System.Boolean");
	b.setRendererEnabled = binder.Method(renderer, "set_enabled", {"System.Boolean"}, "System.Void");
	b.materialCopy = binder.Method(material, ".ctor", {"UnityEngine.Material"}, "System.Void");
	b.hasProperty = binder.Method(material, "HasProperty", {"System.Int32"}, "System.Boolean");
	b.setFloat = binder.Method(material, "SetFloat", {"System.Int32", "System.Single"}, "System.Void");
	b.getFloat = binder.Method(material, "GetFloat", {"System.Int32"}, "System.Single");
	b.enableKeyword = binder.Method(material, "EnableKeyword", {"System.String"}, "System.Void");
	b.disableKeyword = binder.Method(material, "DisableKeyword", {"System.String"}, "System.Void");
	b.setRenderQueue = binder.Method(material, "set_renderQueue", {"System.Int32"}, "System.Void");
	b.setOverrideTag = binder.Method(material, "SetOverrideTag", {"System.String", "System.String"}, "System.Void");
	b.propertyToId = binder.Method(shader, "PropertyToID", {"System.String"}, "System.Int32", true);
	b.destroy = binder.Method(object, "Destroy", {"UnityEngine.Object"}, "System.Void", true);
	b.objectName = binder.Method(object, "get_name", {}, "System.String");
	if (!binder.Errors().empty())
	{
		return moa::MakeError("{}", binder.Errors());
	}
	if (managed.il2cpp_object_new == nullptr || managed.il2cpp_gchandle_new == nullptr ||
	    managed.il2cpp_gchandle_free == nullptr || managed.il2cpp_array_new == nullptr ||
	    managed.il2cpp_class_array_element_size == nullptr || managed.il2cpp_array_object_header_size == nullptr ||
	    managed.il2cpp_array_length == nullptr)
	{
		return moa::MakeError("GameAssembly.dll does not export the object, array or handle functions");
	}
	b.material = material;
	b.meshRendererType = managed.TypeObject(meshRenderer);
	b.lodGroupType = managed.TypeObject(lodGroup);
	b.legacyType = managed.TypeObject(legacy);
	b.movingType = managed.TypeObject(moving);
	return b;
}

GameFadeScene::GameFadeScene(const Managed& managed, FadeBindings bindings)
    : _managed(managed)
    , _b(bindings)
{
}

GameFadeScene::~GameFadeScene()
{
	// The objects go back to the game before this: `OccluderFade::RestoreAll` on the main thread. Here the scene only
	// lets the garbage collector have the objects that it kept.
	for (auto& [object, state] : _objects)
	{
		Release(state.root);
		for (auto& renderer : state.renderers)
		{
			Release(renderer.renderer);
			Release(renderer.materials);
			Release(renderer.copies);
		}
	}
	for (auto& [object, kept] : _found)
	{
		Release(kept);
	}
	Release(_heldTransform);
	Release(_heldRoot);
	Release(_handTransform);
}

GameFadeScene::Kept GameFadeScene::Keep(Il2CppObject* object) const
{
	if (object == nullptr)
	{
		return {};
	}
	return {.object = object, .handle = _managed.il2cpp_gchandle_new(object, false)};
}

void GameFadeScene::Release(Kept& kept) const
{
	if (kept.handle != 0)
	{
		_managed.il2cpp_gchandle_free(kept.handle);
	}
	kept = {};
}

void GameFadeScene::Fail(std::string text)
{
	if (text != _lastError)
	{
		spdlog::error("camera fade: {}", text);
	}
	_lastError = std::move(text);
}

void GameFadeScene::BeginFrame()
{
	// The objects that the last frame found and that the fade did not change go back to the garbage collector.
	for (auto& [object, kept] : _found)
	{
		Release(kept);
	}
	_found.clear();
	ReadHand();
}

void GameFadeScene::ReadHand()
{
	Release(_heldTransform);
	Release(_heldRoot);
	Release(_handTransform);
	if (auto held = _managed.Invoke(_b.currentDragObject, nullptr); held && *held != nullptr)
	{
		if (auto transform = _managed.Invoke(_b.gameObjectTransform, *held); transform && *transform != nullptr)
		{
			_heldTransform = Keep(*transform);
			_heldRoot = Keep(RootOf(*transform));
		}
	}
	if (auto manager = _managed.SingletonInstance("PlayerHandManager"); manager && *manager != nullptr)
	{
		if (auto* hand = ReadField<Il2CppObject*>(*manager, _b.handInstance); hand != nullptr)
		{
			if (auto transform = _managed.Invoke(_b.gameObjectTransform, hand); transform && *transform != nullptr)
			{
				_handTransform = Keep(*transform);
			}
		}
	}
}

Il2CppObject* GameFadeScene::RootOf(Il2CppObject* transform) const
{
	for (auto* type : {_b.legacyType, _b.lodGroupType})
	{
		if (auto found = _managed.Invoke(_b.componentInParent, transform, {type}); found && *found != nullptr)
		{
			auto root = _managed.Invoke(_b.gameObject, *found);
			return root ? *root : nullptr;
		}
	}
	auto root = _managed.Invoke(_b.gameObject, transform);
	return root ? *root : nullptr;
}

bool GameFadeScene::PartOfHand(Il2CppObject* transform) const
{
	for (const auto* parent : {&_heldTransform, &_handTransform})
	{
		if (parent->object == nullptr)
		{
			continue;
		}
		if (auto inside = _managed.Call<bool>(_b.isChildOf, transform, {parent->object}); inside && *inside)
		{
			return true;
		}
	}
	return false;
}

bool GameFadeScene::Held(uint64_t object)
{
	if (_heldRoot.object != nullptr && object == static_cast<uint64_t>(reinterpret_cast<uintptr_t>(_heldRoot.object)))
	{
		return true;
	}
	if (_heldTransform.object == nullptr && _handTransform.object == nullptr)
	{
		return false;
	}
	auto* root = reinterpret_cast<Il2CppObject*>(static_cast<uintptr_t>(object));
	auto transform = _managed.Invoke(_b.gameObjectTransform, root);
	return transform && *transform != nullptr && PartOfHand(*transform);
}

HeldObjectLook GameFadeScene::LookOfHeldObject()
{
	ReadHand();
	HeldObjectLook look;
	auto held = _managed.Invoke(_b.currentDragObject, nullptr);
	if (!held || *held == nullptr)
	{
		return look;
	}
	look.held = true;
	if (auto name = _managed.Invoke(_b.objectName, *held); name && *name != nullptr)
	{
		look.name = _managed.Text(*name);
	}
	look.changedByFade =
	    _heldRoot.object != nullptr && _objects.contains(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(_heldRoot.object)));
	if (!_alphaProperty)
	{
		if (auto id = _managed.Call<int32_t>(_b.propertyToId, nullptr, {_managed.String("_AlphaMultiply")}); id)
		{
			_alphaProperty = *id;
		}
	}
	auto includeInactive = false;
	auto renderers = _managed.Invoke(_b.componentsInChildren, *held, {_b.meshRendererType, &includeInactive});
	if (!renderers || *renderers == nullptr)
	{
		return look;
	}
	for (size_t index = 0; index < _managed.ArrayLength(*renderers); ++index)
	{
		auto* renderer = _managed.ArrayItem(*renderers, index);
		if (auto enabled = _managed.Call<bool>(_b.rendererEnabled, renderer); !enabled || !*enabled)
		{
			continue;
		}
		++look.renderers;
		if (auto mode = _managed.Call<int32_t>(_b.shadowMode, renderer); mode && *mode == k_ShadowsOnly)
		{
			++look.shadowOnly;
		}
		auto materials = _managed.Invoke(_b.sharedMaterials, renderer);
		if (!materials || *materials == nullptr || !_alphaProperty)
		{
			continue;
		}
		for (size_t slot = 0; slot < _managed.ArrayLength(*materials); ++slot)
		{
			auto* material = _managed.ArrayItem(*materials, slot);
			if (material == nullptr)
			{
				continue;
			}
			if (auto has = _managed.Call<bool>(_b.hasProperty, material, {&*_alphaProperty}); !has || !*has)
			{
				continue;
			}
			if (auto value = _managed.Call<float>(_b.getFloat, material, {&*_alphaProperty}); value)
			{
				look.lowestOpacity = look.lowestOpacity ? std::min(*look.lowestOpacity, *value) : *value;
			}
		}
	}
	return look;
}

uint64_t GameFadeScene::ObjectOf(Il2CppObject* collider)
{
	if (collider == nullptr)
	{
		return 0;
	}
	// People, creatures and the things that they carry do not fade.
	if (auto moving = _managed.Invoke(_b.componentInParent, collider, {_b.movingType}); !moving || *moving != nullptr)
	{
		return 0;
	}
	// The object in the hand and the hand do not fade.
	if (auto transform = _managed.Invoke(_b.componentTransform, collider); !transform || PartOfHand(*transform))
	{
		return 0;
	}
	Il2CppObject* owner = nullptr;
	for (auto* type : {_b.legacyType, _b.lodGroupType})
	{
		auto found = _managed.Invoke(_b.componentInParent, collider, {type});
		if (!found)
		{
			Fail(found.GetError().message);
			return 0;
		}
		if (*found != nullptr)
		{
			owner = *found;
			break;
		}
	}
	auto root = _managed.Invoke(_b.gameObject, owner != nullptr ? owner : collider);
	if (!root || *root == nullptr)
	{
		return 0;
	}
	const auto object = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(*root));
	if (!_objects.contains(object) && !_found.contains(object))
	{
		_found.emplace(object, Keep(*root));
	}
	return object;
}

void GameFadeScene::AddObjects(Il2CppObject* collider, std::vector<uint64_t>& found)
{
	if (const auto object = ObjectOf(collider); object != 0 && std::ranges::find(found, object) == found.end())
	{
		found.push_back(object);
	}
}

std::optional<std::vector<uint64_t>> GameFadeScene::ObjectsAt(WorldPoint point, float radius)
{
	auto layers = k_FadeLayers;
	auto triggers = k_IgnoreTriggers;
	auto colliders = _managed.Invoke(_b.overlapSphere, nullptr, {&point, &radius, &layers, &triggers});
	if (!colliders)
	{
		Fail(colliders.GetError().message);
		return std::nullopt;
	}
	std::vector<uint64_t> found;
	const auto count = _managed.ArrayLength(*colliders);
	for (size_t index = 0; index < count; ++index)
	{
		AddObjects(_managed.ArrayItem(*colliders, index), found);
	}
	return found;
}

bool GameFadeScene::Prepare(uint64_t object, ObjectState& state)
{
	auto kept = _found.find(object);
	if (kept == _found.end())
	{
		Fail("the fade asked for an object that no test found");
		return false;
	}
	state.root = std::exchange(kept->second, {});
	_found.erase(kept);
	if (!_alphaProperty)
	{
		auto id = _managed.Call<int32_t>(_b.propertyToId, nullptr, {_managed.String("_AlphaMultiply")});
		if (!id)
		{
			Fail(id.GetError().message);
			return false;
		}
		_alphaProperty = *id;
	}
	auto includeInactive = false;
	auto renderers = _managed.Invoke(_b.componentsInChildren, state.root.object, {_b.meshRendererType, &includeInactive});
	if (!renderers)
	{
		Fail(renderers.GetError().message);
		return false;
	}
	const auto count = _managed.ArrayLength(*renderers);
	if (count == 0 || count > k_MostRenderers)
	{
		return false;
	}
	for (size_t index = 0; index < count; ++index)
	{
		auto* renderer = _managed.ArrayItem(*renderers, index);
		// A renderer that the game turned off shows nothing, so the fade does not touch it.
		auto enabled = _managed.Call<bool>(_b.rendererEnabled, renderer);
		auto mode = _managed.Call<int32_t>(_b.shadowMode, renderer);
		auto materials = _managed.Invoke(_b.sharedMaterials, renderer);
		if (!enabled || !*enabled || !mode || !materials || *materials == nullptr)
		{
			continue;
		}
		// The renderer can be see-through if each of its materials has the opacity of the common shader.
		auto fadeable = _managed.ArrayLength(*materials) > 0;
		for (size_t slot = 0; fadeable && slot < _managed.ArrayLength(*materials); ++slot)
		{
			auto* material = _managed.ArrayItem(*materials, slot);
			auto has = material != nullptr ? _managed.Call<bool>(_b.hasProperty, material, {&*_alphaProperty})
			                               : moa::Result<bool>(false);
			fadeable = has && *has;
		}
		state.renderers.push_back({.renderer = Keep(renderer),
		                           .materials = Keep(*materials),
		                           .copies = {},
		                           .shadowMode = *mode,
		                           .fadeable = fadeable,
		                           .hidden = false,
		                           .faded = false});
	}
	if (auto name = _managed.Invoke(_b.objectName, state.root.object); name && *name != nullptr)
	{
		state.name = _managed.Text(*name);
	}
	const auto fadeable = std::ranges::count_if(state.renderers, [](const RendererState& r) { return r.fadeable; });
	spdlog::info("camera fade: '{}' has {} renderers, {} can be see-through", state.name, state.renderers.size(), fadeable);
	return !state.renderers.empty();
}

bool GameFadeScene::MakeCopies(RendererState& renderer)
{
	const auto count = _managed.ArrayLength(renderer.materials.object);
	auto* copies = _managed.il2cpp_array_new(_b.material, count);
	if (copies == nullptr)
	{
		Fail("the mod could not make an array of materials");
		return false;
	}
	renderer.copies = Keep(copies);
	for (size_t slot = 0; slot < count; ++slot)
	{
		auto* original = _managed.ArrayItem(renderer.materials.object, slot);
		auto* copy = _managed.il2cpp_object_new(_b.material);
		if (copy == nullptr)
		{
			Fail("the mod could not make a material");
			return false;
		}
		if (auto made = _managed.Invoke(_b.materialCopy, copy, {original}); !made)
		{
			Fail(made.GetError().message);
			return false;
		}
		_managed.SetArrayItem(copies, slot, copy);
		// The transparent surface of the common shader: the alpha blends, the copy writes no depth, and the alpha
		// test is off so that the opacity fades the whole surface.
		bool ok = true;
		const auto setFloat = [&](const char* name, float value) {
			auto id = _managed.Call<int32_t>(_b.propertyToId, nullptr, {_managed.String(name)});
			ok = ok && id && _managed.Invoke(_b.setFloat, copy, {&*id, &value}).HasValue();
		};
		setFloat("_Surface", k_SurfaceTransparent);
		setFloat("_Blend", k_BlendAlpha);
		setFloat("_SrcBlend", k_BlendSourceAlpha);
		setFloat("_DstBlend", k_BlendOneMinusSourceAlpha);
		setFloat("_SrcBlendAlpha", k_BlendOne);
		setFloat("_DstBlendAlpha", k_BlendOneMinusSourceAlpha);
		setFloat("_ZWrite", 0.0f);
		setFloat("_AlphaClip", 0.0f);
		ok = ok && _managed.Invoke(_b.disableKeyword, copy, {_managed.String("_ALPHATEST_ON")}).HasValue();
		ok = ok && _managed.Invoke(_b.enableKeyword, copy, {_managed.String("_SURFACE_TYPE_TRANSPARENT")}).HasValue();
		ok = ok && _managed.Invoke(_b.setOverrideTag, copy, {_managed.String("RenderType"), _managed.String("Transparent")})
		               .HasValue();
		auto queue = k_TransparentQueue;
		ok = ok && _managed.Invoke(_b.setRenderQueue, copy, {&queue}).HasValue();
		if (!ok)
		{
			Fail("the mod could not make a see-through material");
			return false;
		}
	}
	return true;
}

bool GameFadeScene::SetHidden(RendererState& renderer, bool hidden)
{
	if (renderer.hidden == hidden)
	{
		return true;
	}
	// A renderer that casts a shadow keeps only its shadow. A renderer with no shadow stops.
	if (renderer.shadowMode == k_ShadowsOff)
	{
		auto enabled = !hidden;
		if (auto set = _managed.Invoke(_b.setRendererEnabled, renderer.renderer.object, {&enabled}); !set)
		{
			Fail(set.GetError().message);
			return false;
		}
	}
	else
	{
		auto mode = hidden ? k_ShadowsOnly : renderer.shadowMode;
		if (auto set = _managed.Invoke(_b.setShadowMode, renderer.renderer.object, {&mode}); !set)
		{
			Fail(set.GetError().message);
			return false;
		}
	}
	renderer.hidden = hidden;
	return true;
}

bool GameFadeScene::SetFaded(RendererState& renderer, bool faded)
{
	if (renderer.faded == faded)
	{
		return true;
	}
	if (faded && renderer.copies.object == nullptr && !MakeCopies(renderer))
	{
		return false;
	}
	auto* materials = faded ? renderer.copies.object : renderer.materials.object;
	if (auto set = _managed.Invoke(_b.setSharedMaterials, renderer.renderer.object, {materials}); !set)
	{
		Fail(set.GetError().message);
		return false;
	}
	renderer.faded = faded;
	return true;
}

bool GameFadeScene::ShowObject(uint64_t object, float opacity, bool hidden)
{
	auto entry = _objects.find(object);
	if (entry == _objects.end())
	{
		ObjectState state;
		if (!Prepare(object, state))
		{
			// Keep the root until `RestoreObject`, which the fade calls next.
			entry = _objects.emplace(object, std::move(state)).first;
			return false;
		}
		entry = _objects.emplace(object, std::move(state)).first;
	}
	for (auto& renderer : entry->second.renderers)
	{
		const auto hide = hidden || (!renderer.fadeable && opacity < k_HideWithoutFade);
		if (!SetHidden(renderer, hide))
		{
			return false;
		}
		if (hide || !renderer.fadeable)
		{
			continue;
		}
		if (!SetFaded(renderer, opacity < 1.0f))
		{
			return false;
		}
		if (renderer.faded)
		{
			for (size_t slot = 0; slot < _managed.ArrayLength(renderer.copies.object); ++slot)
			{
				auto* copy = _managed.ArrayItem(renderer.copies.object, slot);
				auto value = opacity;
				if (auto set = _managed.Invoke(_b.setFloat, copy, {&*_alphaProperty, &value}); !set)
				{
					Fail(set.GetError().message);
					return false;
				}
			}
		}
	}
	return true;
}

void GameFadeScene::RestoreObject(uint64_t object)
{
	auto entry = _objects.find(object);
	if (entry == _objects.end())
	{
		if (auto kept = _found.find(object); kept != _found.end())
		{
			Release(kept->second);
			_found.erase(kept);
		}
		return;
	}
	auto& state = entry->second;
	for (auto& renderer : state.renderers)
	{
		// Each step goes on after an error: a renderer that the game destroyed fails, and the others still go back.
		static_cast<void>(SetHidden(renderer, false));
		static_cast<void>(SetFaded(renderer, false));
		for (size_t slot = 0; slot < _managed.ArrayLength(renderer.copies.object); ++slot)
		{
			if (auto* copy = _managed.ArrayItem(renderer.copies.object, slot); copy != nullptr)
			{
				static_cast<void>(_managed.Invoke(_b.destroy, nullptr, {copy}));
			}
		}
		Release(renderer.renderer);
		Release(renderer.materials);
		Release(renderer.copies);
	}
	Release(state.root);
	_objects.erase(entry);
}

nlohmann::json GameFadeScene::Describe() const
{
	auto objects = nlohmann::json::array();
	for (const auto& [object, state] : _objects)
	{
		objects.push_back(
		    {{"object", std::format("{:#x}", object)},
		     {"name", state.name},
		     {"renderers", state.renderers.size()},
		     {"seeThrough", std::ranges::count_if(state.renderers, [](const RendererState& r) { return r.fadeable; })}});
	}
	return objects;
}

size_t GameFadeScene::MaterialCopies() const
{
	size_t count = 0;
	for (const auto& [object, state] : _objects)
	{
		for (const auto& renderer : state.renderers)
		{
			count += _managed.ArrayLength(renderer.copies.object);
		}
	}
	return count;
}

} // namespace moa_mod::camera
