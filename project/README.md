# GodotCrowdControlDemo

A Demo for the GodotCrowdControl GDExtension. Contains a simple game with effects
and a custom overlay.

Notably as to keep the project as clean as possible, the demo uses a static singleton instead
of an autoload and sets project settings on runtime. This is not recommended.

## How to setup your game for Crowd Control

1. Create your game in the [Crowd Control developer portal](https://dev.crowdcontrol.live/). Its page shows the
   **Application ID** and, under _Game Packs_, the **Game Pack ID** (it starts with `x_`). Leave the integration
   mode on _Public client (PKCE)_: no application secret is needed (or wanted) in the game.
2. Enter them in the project settings (_Crowd Control > Common_):
   - `crowd_control/common/app_id`: the Application ID
   - `crowd_control/common/game_id`: the Game Pack ID
   - `crowd_control/common/game_name`: the name of the game
   - `crowd_control/common/game_version` (optional): the version of your game, e.g. `1.2.0`. It is sent when a
     session starts so the pack version made for that build is used. Falls back to `application/config/version`.

   ![settings](./demo/guide/settings.png)

3. Create a CCEffectEntries resource and set `crowd_control/common/effects` to the resource path.
4. Create named subclasses of CCEffect to add effects to your game. Every effect needs a unique ID (letters,
   numbers and underscores) and has to implement `_trigger()` (`_start()` / `_stop()` for timed effects).
5. Select the CCEffectEntries resource and use _Export Effect Pack..._ at the bottom of the inspector. Upload the
   zip under _Pack Submissions_ in the developer portal and press _Start Testing_: you and your team (see _Team_
   in the portal) can now start sessions from the game.
6. When everything works, _Submit for Review_ in the portal and _Release_ the version once it is approved.

The full documentation is at [developer.crowdcontrol.live/godot](https://developer.crowdcontrol.live/godot/).

![json_manifest](./demo/guide/json_manifest.png)

## Logging in

The plugin implements the Crowd Control 2.0 [PubSub protocol](https://developer.crowdcontrol.live/sockets/).

```gdscript
CrowdControl.connect_to_crowd_control()
# `logged_out` is emitted if there is no (valid) stored login, `logged_in` otherwise.
CrowdControl.logged_out.connect(func(): CrowdControl.login())
# The user has to sign in at `url` in their browser. It is opened automatically
# unless `crowd_control/common/auth/auto_open_url` is disabled.
CrowdControl.auth_code_received.connect(func(code: String, url: String): print(url))
CrowdControl.login_failed.connect(func(message: String): push_error(message))
CrowdControl.logged_in.connect(func(): CrowdControl.start_session())
```

- `login()` no longer takes a platform, the user picks it on the login page.
- The login is stored in `user://crowd_control.cfg` and reused. Disable
  `crowd_control/common/auth/persist_token` to turn this off, call `CrowdControl.logout()` to forget it.
- A login is valid for 24 hours and is extended automatically while the game is connected (`token_refreshed` is
  emitted). Applications created in the developer portal have the `token:extend` scope this needs. For an
  application without it, a stored login with less than 8 hours left is not reused, so the user is asked to log in
  when connecting instead of being logged out in the middle of a session.
- `logged_out` can also be emitted while the game is running (the login expired or was revoked), so keep a way
  to call `login()` reachable from within the game.
- `crowd_control/common/auth/scopes` can restrict the scopes requested for the token. If left empty the token
  gets every scope of the application, which is what you want in almost every case. The plugin needs
  `session:write` (start / stop sessions), `session:control` (report effects) and `profile:read` (streamer name
  and icon).
- `session_start_failed` is emitted if no session could be started, e.g. because no version of the game pack is
  in testing or released, or the account is not on the game's team in the portal.
- The connection is re-established automatically when it drops (the service also closes every socket after two
  hours). `disconnected` and `connection_errored` are emitted when that happens, followed by `connected` and
  `logged_in` once it is back; the session keeps running. Don't treat them as the end of the session.
- Call `CrowdControl.close()` before quitting and wait for `disconnected`, otherwise the session is left open.
- `CrowdControl.get_interact_link()` returns the link viewers use to interact with the streamer.

## Controlling the effect menu

```gdscript
# Hidden effects are not listed at all, disabled ones are listed but can't be bought.
CrowdControl.hide_effects(["bomb", "damageplayer"], "Not during cutscenes")
CrowdControl.show_effects(["bomb", "damageplayer"])
CrowdControl.disable_all_effects("Boss fight")
CrowdControl.enable_all_effects()
# Or per effect:
CrowdControl.get_effect("bomb").visible = false
CrowdControl.get_effect("bomb").sellable = false
```

Changes made while no session is running are remembered and sent along when the next session starts.

Effects can list the IDs of effects they can't run alongside in `conflicts`; requests are rejected while one of
them is running. Requests that could not be started within 55 seconds are answered as a temporary failure (refund).

## Events, metadata and coins

```gdscript
# Events and their arguments are defined in the game pack (`meta.events`).
CrowdControl.trigger_event("level_up", {"levels": 10})
CrowdControl.update_metadata("stars", 120)
# The amount needs the `pubsub.private:read` scope (applications from the developer portal have it), otherwise it is 0.
CrowdControl.coins_exchanged.connect(func(amount: int, user: StreamUser): print(user.get_name(), amount))
# The streamer paused / resumed ordering.
CrowdControl.session_paused.connect(func(paused: bool): print(paused))
```

## Custom effects (development tool)

Effects flagged as `custom` are left out of the effect pack and can be uploaded by the game instead. This needs a
login with the `custom-effects:write` scope (applications from the developer portal have it) and a game pack that
allows custom effects.

```gdscript
if CrowdControl.can_upload_custom_effects():
	CrowdControl.upload_custom_effects(CrowdControl.CUSTOM_EFFECTS_MERGE)
CrowdControl.custom_effects_updated.connect(func(success: bool, message: String): print(success, message))
```

## Effect pack

Select the `CCEffectEntries` resource and use _Export Effect Pack..._ at the bottom of the inspector (or call
`save_pack_zip(path)` on the resource). The zip contains `menu.json` and the icon of every effect that has one
(`assets/icons/<effect id>.png`), which is what the [developer portal](https://dev.crowdcontrol.live/) expects under
_Pack Submissions_. Pick _Menu JSON only_ in the dialog to save just the JSON, _JSON Manifest_ copies it to the
clipboard.

- Effects without an ID, with an invalid ID or a duplicate ID are left out (an error is printed), as are effects
  flagged as `custom`.
- `visible` and `sellable` are runtime state and not part of the pack, they are reported when a session starts.
- Translations (`locales/<locale>.json` inside the zip) are not generated.

©SilicDev 2023-2024, Warp World, Inc. 2023-2027  
Sprites kindly provided by [Sigmund_Froid](https://www.twitch.tv/sigmund_froid)
