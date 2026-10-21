/******************************************************************************
 * Copyright (c) 2026 Masters of Albion Reforged developers
 *****************************************************************************/

#pragma once

#include <cstdint>

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <common/Result.h>
#include <core/CameraFade.h>

#include "mod_game/Managed.h"

namespace moa_mod::camera
{

/// The names of Unity and of the game that the fade of the objects uses. `Bind` finds all of them, or returns an
/// error that names each missing one.
struct FadeBindings
{
	const MethodInfo* overlapSphere = nullptr;
	const MethodInfo* gameObject = nullptr;          ///< `Component.gameObject`.
	const MethodInfo* componentTransform = nullptr;  ///< `Component.transform`.
	const MethodInfo* gameObjectTransform = nullptr; ///< `GameObject.transform`.
	const MethodInfo* isChildOf = nullptr;           ///< `Transform.IsChildOf(Transform)`.
	const MethodInfo* currentDragObject = nullptr;   ///< The game object that the hand holds, or null.
	const MethodInfo* componentInParent = nullptr;
	const MethodInfo* componentsInChildren = nullptr; ///< `GameObject.GetComponentsInChildren(Type, bool)`.
	const MethodInfo* sharedMaterials = nullptr;
	const MethodInfo* setSharedMaterials = nullptr;
	const MethodInfo* shadowMode = nullptr;
	const MethodInfo* setShadowMode = nullptr;
	const MethodInfo* rendererEnabled = nullptr;
	const MethodInfo* setRendererEnabled = nullptr;
	const MethodInfo* materialCopy = nullptr; ///< `new Material(Material)`.
	const MethodInfo* hasProperty = nullptr;  ///< `Material.HasProperty(int)`.
	const MethodInfo* setFloat = nullptr;     ///< `Material.SetFloat(int, float)`.
	const MethodInfo* getFloat = nullptr;     ///< `Material.GetFloat(int)`.
	const MethodInfo* enableKeyword = nullptr;
	const MethodInfo* disableKeyword = nullptr;
	const MethodInfo* setRenderQueue = nullptr;
	const MethodInfo* setOverrideTag = nullptr;
	const MethodInfo* propertyToId = nullptr;
	const MethodInfo* destroy = nullptr;
	const MethodInfo* objectName = nullptr;
	Il2CppClass* material = nullptr;
	size_t handInstance = 0; ///< The field of the hand manager with the game object of the hand.
	Il2CppObject* meshRendererType = nullptr;
	Il2CppObject* lodGroupType = nullptr;
	Il2CppObject* legacyType = nullptr; ///< The base of the buildings, the decorations and the trees of the game.
	Il2CppObject* movingType = nullptr; ///< The base of the people, the creatures and the things that move.

	[[nodiscard]] static moa::Result<FadeBindings> Bind(const Managed& managed);
};

/// The objects of the running game for the fade. An object is the root of a building, a tree, a rock or a cliff:
/// the game object of its component of the game, or of its group of levels of detail, or of its collider. The object
/// in the hand and the parts of the hand are never objects of the fade.
///
/// A see-through object gets copies of its materials with the transparent surface of the common shader of the game
/// and the opacity in `_AlphaMultiply`. A renderer with another shader is hidden in place of a fade. A hidden
/// renderer draws only its shadow. The scene never changes a shared material, and it gives back each renderer at
/// `RestoreObject`. Call it only on the main thread.
class GameFadeScene final: public FadeScene
{
public:
	GameFadeScene(const Managed& managed, FadeBindings bindings);
	GameFadeScene(const GameFadeScene&) = delete;
	GameFadeScene& operator=(const GameFadeScene&) = delete;
	~GameFadeScene() override;

	void BeginFrame() override;
	[[nodiscard]] std::optional<std::vector<uint64_t>> ObjectsAt(WorldPoint point, float radius) override;
	[[nodiscard]] bool ShowObject(uint64_t object, float opacity, bool hidden) override;
	void RestoreObject(uint64_t object) override;
	[[nodiscard]] bool Held(uint64_t object) override;

	/// How the object in the hand shows now. Read the hand again: a call can come outside the frame of the fade.
	[[nodiscard]] HeldObjectLook LookOfHeldObject();

	/// The last error of the scene, for the log. Empty if there is none.
	[[nodiscard]] const std::string& LastError() const { return _lastError; }
	/// The count of the materials that the scene made and did not destroy yet.
	[[nodiscard]] size_t MaterialCopies() const;
	/// Each changed object: its name, its renderers and the renderers that can be see-through.
	[[nodiscard]] nlohmann::json Describe() const;

private:
	/// A managed object that the scene keeps alive with a handle of the garbage collector.
	struct Kept
	{
		Il2CppObject* object = nullptr;
		uintptr_t handle = 0;
	};
	/// The state of one renderer of a changed object.
	struct RendererState
	{
		Kept renderer;
		Kept materials; ///< The shared materials of the game.
		Kept copies;    ///< The see-through copies, or nothing.
		int32_t shadowMode = 1;
		bool fadeable = false;
		bool hidden = false; ///< The scene hides the renderer now.
		bool faded = false;  ///< The renderer shows the copies now.
	};
	struct ObjectState
	{
		Kept root;
		std::vector<RendererState> renderers;
		std::string name; ///< The name of the game object, for the state and the log.
	};

	[[nodiscard]] Kept Keep(Il2CppObject* object) const;
	void Release(Kept& kept) const;
	/// The object of a collider, or zero if the collider is not on a building, a tree, a rock or a cliff. The scene
	/// keeps the object alive until the next frame, or until `RestoreObject` if the fade changes it.
	[[nodiscard]] uint64_t ObjectOf(Il2CppObject* collider);
	/// Add the objects of the colliders of a test, each one time.
	void AddObjects(Il2CppObject* collider, std::vector<uint64_t>& found);
	/// Read the object in the hand and the hand of this frame.
	void ReadHand();
	/// The root object of a transform by the rules of `ObjectOf`, without the rule for the things that move. Null if
	/// there is none.
	[[nodiscard]] Il2CppObject* RootOf(Il2CppObject* transform) const;
	/// True if a transform is the object in the hand, a part of it, or a part of the hand.
	[[nodiscard]] bool PartOfHand(Il2CppObject* transform) const;
	/// Read the renderers of an object. Return false on an error.
	[[nodiscard]] bool Prepare(uint64_t object, ObjectState& state);
	/// Make the see-through copies of the materials of a renderer. Return false on an error.
	[[nodiscard]] bool MakeCopies(RendererState& renderer);
	[[nodiscard]] bool SetHidden(RendererState& renderer, bool hidden);
	[[nodiscard]] bool SetFaded(RendererState& renderer, bool faded);
	void Fail(std::string text);

	const Managed& _managed;
	FadeBindings _b;
	std::optional<int32_t> _alphaProperty;    ///< The identifier of `_AlphaMultiply`.
	std::map<uint64_t, ObjectState> _objects; ///< The objects that the fade changed.
	std::map<uint64_t, Kept> _found;          ///< The objects that the tests of this frame found.
	Kept _heldTransform;                      ///< The transform of the object in the hand in this frame, or nothing.
	Kept _heldRoot;                           ///< The root object of the object in the hand, or nothing.
	Kept _handTransform;                      ///< The transform of the hand in this frame, or nothing.
	std::string _lastError;
};

} // namespace moa_mod::camera
