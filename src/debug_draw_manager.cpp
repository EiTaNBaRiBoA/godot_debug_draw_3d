#include "debug_draw_manager.h"

#include "2d/debug_draw_2d.h"
#include "2d/grouped_text.h"
#include "3d/debug_draw_3d.h"
#include "utils/utils.h"

GODOT_WARNING_DISABLE()
#include <godot_cpp/classes/config_file.hpp>
#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/global_constants.hpp>
#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/classes/main_loop.hpp>
#include <godot_cpp/classes/node2d.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/classes/window.hpp>
GODOT_WARNING_RESTORE()

DebugDrawManager *DebugDrawManager::singleton = nullptr;

using namespace godot;

void DebugDrawManager::_bind_methods() {
	// ClassDB::bind_method(D_METHOD(NAMEOF(get_title)), &DebugDrawGraph::get_title);
	ClassDB::bind_method(D_METHOD(NAMEOF(_integrate_into_engine)), &DebugDrawManager::_integrate_into_engine);
	ClassDB::bind_method(D_METHOD(NAMEOF(_on_scene_changed)), &DebugDrawManager::_on_scene_changed);
}

Node *DebugDrawManager::_get_current_scene() {
#ifndef DISABLE_DEBUG_RENDERING
	auto ST = SCENE_TREE();
	if (IS_EDITOR_HINT()) {
		return ST->get_edited_scene_root();
	}
	return ST->get_current_scene();
#else
	return nullptr;
#endif
}

void DebugDrawManager::_connect_scene_changed() {
#ifndef DISABLE_DEBUG_RENDERING
	// Skip when exiting the tree and finish this loop
	if (is_closing) {
		return;
	}

	Node *scene = _get_current_scene();
	if (scene && UtilityFunctions::is_instance_valid(scene)) {
		scene->connect(StringName("tree_exiting"), Callable(this, NAMEOF(_on_scene_changed)).bindv(Array::make(false)), CONNECT_ONE_SHOT | CONNECT_DEFERRED);
		return;
	}

	SCENE_TREE()->connect(StringName("tree_changed"), Callable(this, NAMEOF(_on_scene_changed)).bindv(Array::make(true)), CONNECT_ONE_SHOT | CONNECT_DEFERRED);
#endif
}

void DebugDrawManager::_on_scene_changed(bool _is_scene_null) {
#ifndef DISABLE_DEBUG_RENDERING
	if (!is_current_scene_is_null || is_current_scene_is_null != _is_scene_null) {
		DEV_PRINT("Scene changed! clear_all()");
	}

	is_current_scene_is_null = _is_scene_null;
	_connect_scene_changed();
#endif
}

void DebugDrawManager::_integrate_into_engine() {
	// Assigned here because it is created inside the `GDExtension Initialize` function.
	// 1. Create in Initialize and assign Singleton
	// 2. Call class constructor after Initialize to get default values of properties
	// but singleton was already assigned..
	// Other classes will not face similar problems
	ASSIGN_SINGLETON(DebugDrawManager);

	SCENE_ROOT()->add_child(this);
	SCENE_ROOT()->move_child(this, 0);
	set_process(true);

	// Need to be call 'deferred'
	debug_draw_3d_singleton->init(this);
	debug_draw_2d_singleton->init(this);

#ifndef DISABLE_DEBUG_RENDERING
	// Useful nodes and names:
	// CanvasItemEditor - probably 2D editor viewport
	// Node3DEditorViewportContainer - base 3D viewport
	// Node3DEditorViewportContainer/Node3DEditorViewport - base of 1 of 4 viewports
	// Node3DEditorViewportContainer/Node3DEditorViewport/SubViewportContainer/SubViewport/Camera3D

	// Funny hack to get default font
	{
		Control *c = memnew(Control);
		SCENE_ROOT()->add_child(c);
		debug_draw_2d_singleton->_font = c->get_theme_default_font();
		c->queue_free();
	}

	// Setup default text group
	debug_draw_2d_singleton->grouped_text->end_text_group();

	if (IS_EDITOR_HINT()) {
		String editor3d = "Node3DEditorViewportContainer";
		String subviewport = SubViewport::get_class_static();
		Node *res = Utils::find_node_by_class(SCENE_ROOT(), editor3d);

		Node *n = res->get_child(0)->get_child(0);
		n->set_meta("UseParentSize", true);
		debug_draw_2d_singleton->default_canvas = (Control *)n;

		// actual tree for godot 4.0 beta 14
		//
		//	0. VSplitContainer
		//		0. Node3DEditorViewportContainer >>> res
		//			0. Node3DEditorViewport
		//				0. SubViewportContainer >>> res->get_child(0)->get_child(0)
		//					0. SubViewport
		//						0. Camera3D
		//				1. ...
		//			1. Node3DEditorViewport
		//				0. SubViewportContainer
		//					0. SubViewport
		//  ......

		std::vector<SubViewport *> editor_viewports;
		TypedArray<Node> viewports_root = res->get_children();
		for (int i = 0; i < viewports_root.size(); i++) {
			Node *node = cast_to<Node>(viewports_root[i]);
			if (node) {
				SubViewport *sub_view = cast_to<SubViewport>(Utils::find_node_by_class(node, subviewport));
				if (sub_view) {
					editor_viewports.push_back(sub_view);
				}
			}
		}
		debug_draw_3d_singleton->set_custom_editor_viewport(editor_viewports);

		/*
		// Used to explore the editor tree.
		auto f = FileAccess::open("user://tree.txt", FileAccess::WRITE);
		f->store_string(Utils::get_scene_tree_as_string(res->get_parent()->get_parent()));
		*/
	} else {
		// Create canvas item and canvas layer
		auto _canvas_layer = memnew(CanvasLayer);
		_canvas_layer->set_layer(64);
		debug_draw_2d_singleton->_canvas_layer = _canvas_layer;

		auto default_canvas = memnew(Control);
		default_canvas->set_name("DebugDrawDefaultCanvas");
		((Control *)default_canvas)->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
		((Control *)default_canvas)->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
		debug_draw_2d_singleton->default_canvas = default_canvas;

		add_child(_canvas_layer);
		_canvas_layer->add_child(default_canvas);
	}

	debug_draw_2d_singleton->set_custom_canvas(debug_draw_2d_singleton->custom_canvas);
	debug_draw_3d_singleton->_set_base_world_node(SCENE_ROOT());
	_connect_scene_changed();
#endif
}

DebugDrawManager::DebugDrawManager() {
}

DebugDrawManager::~DebugDrawManager() {
	UNASSIGN_SINGLETON(DebugDrawManager);
}

void DebugDrawManager::init() {
	// Draw everything after calls from scripts to avoid lagging
	set_process_priority(INT32_MAX);

	debug_draw_3d_singleton = memnew(DebugDraw3D);
	Engine::get_singleton()->register_singleton(NAMEOF(DebugDraw3D), debug_draw_3d_singleton);
	Engine::get_singleton()->register_singleton("Dbg3", debug_draw_3d_singleton);

	debug_draw_2d_singleton = memnew(DebugDraw2D);
	Engine::get_singleton()->register_singleton(NAMEOF(DebugDraw2D), debug_draw_2d_singleton);
	Engine::get_singleton()->register_singleton("Dbg2", debug_draw_2d_singleton);

	call_deferred(NAMEOF(_integrate_into_engine));
}

void DebugDrawManager::_process(double delta) {
#ifndef DISABLE_DEBUG_RENDERING
	DebugDraw3D::get_singleton()->process(get_process_delta_time());
	DebugDraw2D::get_singleton()->process(get_process_delta_time());
#endif

	log_flush_time += get_process_delta_time();
	if (log_flush_time > 0.25f) {
		log_flush_time -= 0.25f;
		Utils::print_logs();
	}
}

void DebugDrawManager::_exit_tree() {
	is_closing = true;

	if (debug_draw_3d_singleton) {
		Engine::get_singleton()->unregister_singleton(NAMEOF(DebugDraw3D));
		Engine::get_singleton()->unregister_singleton("Dbg3");
		memdelete(debug_draw_3d_singleton);
		debug_draw_3d_singleton = nullptr;
	}

	if (debug_draw_2d_singleton) {
		Engine::get_singleton()->unregister_singleton(NAMEOF(DebugDraw2D));
		Engine::get_singleton()->unregister_singleton("Dbg2");
		memdelete(debug_draw_2d_singleton);
		debug_draw_2d_singleton = nullptr;
	}
}