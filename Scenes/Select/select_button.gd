extends Button

@export var global_key: String = ""

func _pressed() -> void:
	$FileDialog.popup_file_dialog()

func _on_file_dialog_dir_selected(dir: String) -> void:
	if global_key.is_empty():
		push_error("select_button: global_key not set on '%s'" % name)
		return
	Global.set(global_key, dir)
