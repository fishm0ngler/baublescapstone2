extends Node2D

var state: Dictionary = {}
var registry: Dictionary = {}  # card_name (String) -> Texture2D

var _card_nodes: Dictionary = {}  # card_id (String) -> Sprite2D


func _ready() -> void:
	
	# ensure variables are set
	if Global.deck.is_empty() or Global.ruleset.is_empty():
		push_error("_ready: Global.deck or Global.ruleset not set")
		return
	_load_deck_registry(Global.deck)
	_load_ruleset(Global.ruleset)
	if $Sandbox.get_script() == null:
		push_error("_ready: Sandbox script failed to attach")
		return
	var result = $Sandbox.sandbox_init(randi())
	if not result is String:
		push_error("_ready: sandbox_init() did not return a String")
		return
	_apply_state(result)


func do_action(action: String, target: String) -> void:
	if $Sandbox.get_script() == null:
		push_error("do_action: no script on Sandbox node")
		return
	var result = $Sandbox.sandbox_update(action, target)
	if not result is String:
		push_error("do_action: sandbox_update() did not return a String")
		return
	_apply_state(result)


func _load_deck_registry(deck_folder: String) -> void:
	registry.clear()
	
	# load and parse registry file
	var registry_path := deck_folder.path_join("registry.json")
	var file := FileAccess.open(registry_path, FileAccess.READ)
	if file == null:
		push_error("_load_deck_registry: cannot open '%s'" % registry_path)
		return
	var parsed = JSON.parse_string(file.get_as_text())
	file.close()
	if not parsed is Dictionary:
		push_error("_load_deck_registry: registry.json is not a JSON object")
		return
	
	# create in-memory registry
	for card_name in parsed:
		var image := Image.load_from_file(deck_folder.path_join(parsed[card_name]))
		if image == null:
			push_warning("_load_deck_registry: missing image for '%s'" % card_name)
			continue
		registry[card_name] = ImageTexture.create_from_image(image)


func _load_ruleset(ruleset_folder: String) -> void:
	var elf_path := ruleset_folder.path_join(ruleset_folder.get_file() + ".elf")
	var script = load(elf_path)
	if script == null:
		push_error("_load_ruleset: cannot load ELF at '%s'" % elf_path)
		return
	$Sandbox.set_script(script)


func _apply_state(state_json: String) -> void:
	
	# parse and validate
	var parsed = JSON.parse_string(state_json)
	if not parsed is Dictionary:
		push_error("_apply_state: not a JSON object")
		return
	if not parsed.has("cards") or not parsed["cards"] is Dictionary:
		push_error("_apply_state: 'cards' must be a Dictionary")
		return
	if not parsed.has("zones") or not parsed["zones"] is Dictionary:
		push_error("_apply_state: 'zones' must be a Dictionary")
		return
	if not parsed.has("meta") or not parsed["meta"] is Dictionary:
		push_error("_apply_state: 'meta' must be a Dictionary")
		return
	if not parsed["meta"].has("history") or not parsed["meta"]["history"] is Array:
		push_error("_apply_state: 'meta.history' must be an Array")
		return
	
	# handle cards
	for card_id in parsed["cards"]:
		var card = parsed["cards"][card_id]
		if not card is Dictionary:
			push_error("_apply_state: card '%s' must be a Dictionary" % card_id)
			return
		for field in ["shown_face", "faces", "position", "scale", "zone"]:
			if not card.has(field):
				push_error("_apply_state: card '%s' missing field '%s'" % [card_id, field])
				return
		if not card["faces"] is Dictionary:
			push_error("_apply_state: card '%s' faces must be a Dictionary" % card_id)
			return
	
	# handle zones
	for zone_name in parsed["zones"]:
		var zone = parsed["zones"][zone_name]
		if not zone is Dictionary:
			push_error("_apply_state: zone '%s' must be a Dictionary" % zone_name)
			return
		for field in ["cards", "top_left", "bottom_right"]:
			if not zone.has(field):
				push_error("_apply_state: zone '%s' missing field '%s'" % [zone_name, field])
				return
		if not zone["cards"] is Array:
			push_error("_apply_state: zone '%s'.cards must be an Array" % zone_name)
			return
	
	# validate ui elements (optional field)
	for element_id in parsed.get("ui", {}):
		var el = parsed["ui"][element_id]
		if not el is Dictionary:
			push_error("_apply_state: ui element '%s' must be a Dictionary" % element_id)
			return
		for field in ["top_left", "bottom_right", "text"]:
			if not el.has(field):
				push_error("_apply_state: ui element '%s' missing field '%s'" % [element_id, field])
				return

	state = parsed
	_render()
	queue_redraw()


func _input(event: InputEvent) -> void:
	if state.is_empty():
		return

	if event is InputEventMouseButton \
	and event.button_index == MOUSE_BUTTON_LEFT \
	and event.pressed:
		
		var pos := (event as InputEventMouseButton).global_position
		var card_id := _card_at(pos)
		if not card_id.is_empty():
			do_action("click", card_id)
			return
		var element_id := _ui_at(pos)
		if not element_id.is_empty():
			do_action("click", element_id)
			return
		var zone_name := _zone_at(pos)
		do_action("click", zone_name)

	elif event is InputEventKey and event.pressed and not event.echo:
		var key := OS.get_keycode_string(event.keycode).to_lower()
		do_action(key, "")


# HELPERS: TARGET ID

func _card_at(pos: Vector2) -> String:
	var best_id := ""
	var best_z := -INF
	for card_id in _card_nodes:
		var sprite: Sprite2D = _card_nodes[card_id]
		if sprite.get_rect().has_point(sprite.to_local(pos)):
			if sprite.z_index > best_z:
				best_z = sprite.z_index
				best_id = card_id
	return best_id


func _ui_at(pos: Vector2) -> String:
	for element_id in state.get("ui", {}):
		var el: Dictionary = state["ui"][element_id]
		var rect := Rect2(
			Vector2(el["top_left"][0], el["top_left"][1]),
			Vector2(el["bottom_right"][0] - el["top_left"][0],
					el["bottom_right"][1] - el["top_left"][1])
		)
		if rect.has_point(pos):
			return element_id
	return ""


func _zone_at(pos: Vector2) -> String:
	for zone_name in state.get("zones", {}):
		var zone: Dictionary = state["zones"][zone_name]
		var rect := Rect2(
			Vector2(zone["top_left"][0], zone["top_left"][1]),
			Vector2(zone["bottom_right"][0] - zone["top_left"][0],
					zone["bottom_right"][1] - zone["top_left"][1])
		)
		if rect.has_point(pos):
			return zone_name
	return ""


func _draw() -> void:
	for zone_name in state.get("zones", {}):
		var zone: Dictionary = state["zones"][zone_name]
		var rect := Rect2(
			Vector2(zone["top_left"][0], zone["top_left"][1]),
			Vector2(zone["bottom_right"][0] - zone["top_left"][0],
					zone["bottom_right"][1] - zone["top_left"][1])
		)
		draw_rect(rect, Color(1, 1, 1, 0.15), true)
		draw_rect(rect, Color(1, 1, 1, 0.6), false, 2.0)

	for element_id in state.get("ui", {}):
		var el: Dictionary = state["ui"][element_id]
		var rect := Rect2(
			Vector2(el["top_left"][0], el["top_left"][1]),
			Vector2(el["bottom_right"][0] - el["top_left"][0],
					el["bottom_right"][1] - el["top_left"][1])
		)
		var color := Color(0.15, 0.15, 0.15, 0.9)
		if el.has("color"):
			var c = el["color"]
			color = Color(c[0], c[1], c[2], c[3])
		draw_rect(rect, color, true)
		draw_rect(rect, Color(1, 1, 1, 0.8), false, 2.0)
		var font := ThemeDB.fallback_font
		var font_size := ThemeDB.fallback_font_size
		var text_size := font.get_string_size(el["text"], HORIZONTAL_ALIGNMENT_LEFT, -1, font_size)
		var text_pos := rect.get_center() - text_size / 2.0
		draw_string(font, text_pos, el["text"], HORIZONTAL_ALIGNMENT_LEFT, -1, font_size, Color(1, 1, 1, 1))


func _render() -> void:
	# remove nodes for cards no longer in state
	for card_id in _card_nodes.keys():
		if not state["cards"].has(card_id):
			_card_nodes[card_id].queue_free()
			_card_nodes.erase(card_id)

	# add or update a node for each card in state
	for card_id in state["cards"]:
		var card: Dictionary = state["cards"][card_id]
		var pos := Vector2(card["position"][0], card["position"][1])
		var z := int(card["position"][2])
		var scale := Vector2(card["scale"][0], card["scale"][1])

		if _card_nodes.has(card_id):
			_card_nodes[card_id].position = pos
			_card_nodes[card_id].z_index = z
			_card_nodes[card_id].scale = scale
			_card_nodes[card_id].texture = registry.get(card["shown_face"])
		else:
			var sprite := Sprite2D.new()
			sprite.texture = registry.get(card["shown_face"])
			if sprite.texture == null:
				push_warning("_render: no texture in registry for '%s'" % card["shown_face"])
			sprite.position = pos
			sprite.z_index = z
			sprite.scale = scale
			add_child(sprite)
			_card_nodes[card_id] = sprite
