extends PanelContainer


@export var initial_focus: Control
@export var login_button: Button
@export var status_label: Label


func _ready() -> void:
	CrowdControl.auth_code_received.connect(_on_CrowdControl_auth_code_received)
	CrowdControl.login_failed.connect(_on_CrowdControl_login_failed)
	CrowdControl.logged_out.connect(_reset)


func _reset() -> void:
	login_button.disabled = false
	status_label.text = ""


func _on_login_pressed() -> void:
	# Requests a login code. The user picks their platform (Twitch, YouTube, ...)
	# on the page that opens in their browser.
	login_button.disabled = true
	status_label.text = "Requesting login..."
	CrowdControl.login()


func _on_CrowdControl_auth_code_received(code: String, url: String) -> void:
	# The plugin opens the URL on its own unless
	# crowd_control/common/auth/auto_open_url is disabled.
	login_button.disabled = false
	status_label.text = "Sign in with your browser to continue.\nCode: %s\n%s" % [code, url]


func _on_CrowdControl_login_failed(message: String) -> void:
	login_button.disabled = false
	status_label.text = "Login failed: %s" % message


func _on_back_pressed() -> void:
	get_parent().hide()
	CrowdControl.close()
