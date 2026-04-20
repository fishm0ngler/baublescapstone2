extends Node2D


func _ready() -> void:
	$Start.disabled = true


func _on_selection_changed(_dir: String) -> void:
	$Start.disabled = Global.ruleset.is_empty() or Global.deck.is_empty()


func _on_start_pressed() -> void:
	if Global.ruleset.is_empty():
		push_error("_on_start_pressed: no ruleset selected")
		return
	if Global.deck.is_empty():
		push_error("_on_start_pressed: no deck selected")
		return
	get_tree().change_scene_to_file("res://Scenes/Sandbox/TestSandbox.tscn")
